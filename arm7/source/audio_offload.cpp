#include <nds.h>
#include <calico.h>
#include <cstring>
#include "mos6502/mos6502.h"
#include "nds_acp_ipc.h"

namespace {

constexpr uint16_t ACP_RESET = 0;
constexpr uint16_t ACP_NMI = 1;
constexpr uint16_t ACP_RATE = 6;
constexpr uint16_t ACP_RAM_SIZE = 4096;

struct A7AcpState {
    uint8_t ram[ACP_RAM_SIZE];
    mos6502* cpu;
    int16_t irqCounter;
    uint8_t irqRate;
    bool running;
    bool resetting;
    uint8_t dacReg;
    uint16_t clksPerHostSample;
    uint32_t cycles_per_sample;
    uint8_t clkMult;
    uint64_t cycle_counter;
    uint32_t shared_ram_ptr;
    uint32_t shared_ram_lo20;
    uint16_t shared_ram_sync_gen;
    bool has_shared_ram_ptr;
    int volume;
    bool isMuted;
    bool isPaused;
    uint16_t output_hz;
    uint16_t chunk_samples;
    uint8_t buffer_index;
    alignas(4) int16_t pcm_buffers[2][256];
};

static A7AcpState s_acp;

static Thread s_audioThread;
alignas(8) static uint8_t s_audioThreadStack[4096];
static Mailbox s_audioMailbox;
static uint32_t s_audioMailboxSlots[128];

static uint8_t A7MemoryRead(uint16_t address) {
    return s_acp.ram[address & 0x0FFF];
}

static void A7MemoryWrite(uint16_t address, uint8_t value) {
    s_acp.ram[address & 0x0FFF] = value;
    if (address & 0x8000) {
        s_acp.dacReg = value;
    }
}

static void A7CPUStopped() {
}

static void HandleAcpRegisterWrite(uint8_t reg, uint8_t value) {
    switch (reg & 7) {
        case ACP_RESET:
            s_acp.resetting = true;
            s_acp.irqCounter = 255;
            break;
        case ACP_NMI:
            if (s_acp.cpu) {
                s_acp.cpu->NMI();
                s_acp.cpu->RunOptimized((int32_t)s_acp.cycles_per_sample, s_acp.cycle_counter);
            }
            break;
        case ACP_RATE:
            s_acp.irqRate = (((value << 1) & 0xFE) | (value & 1));
            s_acp.running = (value & 0x80) != 0;
            s_acp.cycles_per_sample = (uint32_t)s_acp.irqRate * s_acp.clkMult;
            break;
        default:
            break;
    }
}

static void HandleControlWrite(uint16_t opcode, uint8_t value) {
    switch (opcode) {
        case NDS_ACP_CTRL_VOLUME:
            s_acp.volume = value;
            break;
        case NDS_ACP_CTRL_MUTE:
            s_acp.isMuted = value != 0;
            break;
        case NDS_ACP_CTRL_PAUSE:
            s_acp.isPaused = value != 0;
            break;
        default:
            break;
    }
}

static void SyncRamFromArm9() {
    if (!s_acp.has_shared_ram_ptr || s_acp.shared_ram_ptr == 0) {
        return;
    }
    const uint8_t* src = reinterpret_cast<const uint8_t*>(s_acp.shared_ram_ptr);
    memcpy(s_acp.ram, src, ACP_RAM_SIZE);
}

static void HandlePxiMessage(uint32_t msg) {
    uint32_t cmd = ndsAcpMsgCmd(msg);
    uint32_t arg = ndsAcpMsgArg(msg);
    uint8_t value = (uint8_t)ndsAcpMsgValue(msg);

    switch (cmd) {
        case NDS_ACP_CMD_RAM_WRITE:
            s_acp.ram[arg & 0x0FFF] = value;
            break;
        case NDS_ACP_CMD_SET_RAM_PTR_LO:
            s_acp.shared_ram_lo20 = (arg & 0x0FFF) | ((uint32_t)value << 12);
            break;
        case NDS_ACP_CMD_SET_RAM_PTR_HI:
            s_acp.shared_ram_ptr = s_acp.shared_ram_lo20 | ((arg & 0x0FFF) << 20);
            s_acp.has_shared_ram_ptr = true;
            break;
        case NDS_ACP_CMD_RAM_SYNC:
            s_acp.shared_ram_sync_gen = (uint16_t)(((arg & 0xFF) << 8) | value);
            SyncRamFromArm9();
            break;
        case NDS_ACP_CMD_REG_WRITE:
            HandleAcpRegisterWrite((uint8_t)arg, value);
            break;
        case NDS_ACP_CMD_CONTROL:
            HandleControlWrite((uint16_t)arg, value);
            break;
        default:
            break;
    }
}

static void FillAudioChunk(int16_t* out, int sampleCount) {
    if (!s_acp.running) {
        memset(out, 0, sampleCount * sizeof(int16_t));
        return;
    }
    for (int i = 0; i < sampleCount; i++) {
        int sample = ((int)s_acp.dacReg - 128) * s_acp.volume;
        if (s_acp.isMuted || s_acp.isPaused) {
            sample = 0;
        } else {
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
        }
        out[i] = (int16_t)sample;

        if (s_acp.isPaused) {
            continue;
        }

        s_acp.irqCounter -= s_acp.clksPerHostSample;
        if (s_acp.irqCounter < 0) {
            if (s_acp.resetting) {
                s_acp.resetting = false;
                if (s_acp.cpu) {
                    s_acp.cpu->Reset();
                }
            }
            s_acp.irqCounter += s_acp.irqRate;
            s_acp.cycle_counter = 0;

            if (s_acp.running && s_acp.cpu) {
                s_acp.cpu->IRQ();
                s_acp.cpu->ClearIRQ();
                s_acp.cpu->RunOptimized((int32_t)s_acp.cycles_per_sample, s_acp.cycle_counter);
            }
        }
    }
}

static void PlayChunk(int16_t* samples) {
    // len is in 32-bit words for PCM channels.
    const unsigned len_words = s_acp.chunk_samples / 2;
    const unsigned timer = soundTimerFromHz(s_acp.output_hz);

    soundChPreparePcm(0, 127, SoundVolDiv_1, 64, timer,
        SoundMode_OneShot, SoundFmt_Pcm16, samples, 0, len_words);
    soundChStart(0);
}

static int AudioThreadMain(void*) {
    mailboxPrepare(&s_audioMailbox, s_audioMailboxSlots, sizeof(s_audioMailboxSlots) / sizeof(s_audioMailboxSlots[0]));
    pxiSetMailbox((PxiChannel)NDS_ACP_PXI_CHANNEL, &s_audioMailbox);

    powerOn(POWER_SOUND);
    soundSetMixerVolume(127);
    soundSetMixerConfig(SoundOutSrc_Mixer, SoundOutSrc_Mixer, false, false);
    REG_SOUNDCNT |= SOUNDCNT_ENABLE;

    const uint32_t chunk_usec = (1000000u * s_acp.chunk_samples) / s_acp.output_hz;

    while (true) {
        uint32_t msg = 0;
        while (mailboxTryRecv(&s_audioMailbox, &msg)) {
            HandlePxiMessage(msg);
        }

        int16_t* out = s_acp.pcm_buffers[s_acp.buffer_index];
        FillAudioChunk(out, s_acp.chunk_samples);
        PlayChunk(out);
        s_acp.buffer_index ^= 1;

        threadSleep(chunk_usec);
    }

    return 0;
}

} // namespace

extern "C" void gtAudioOffloadInit(void) {
    memset(&s_acp, 0, sizeof(s_acp));
    s_acp.cpu = new mos6502(A7MemoryRead, A7MemoryWrite, A7CPUStopped, NULL);
    s_acp.irqCounter = 0;
    s_acp.irqRate = 0;
    s_acp.running = false;
    s_acp.resetting = false;
    s_acp.dacReg = 0;
    s_acp.clkMult = 4;
    s_acp.cycles_per_sample = 1024;
    s_acp.shared_ram_ptr = 0;
    s_acp.shared_ram_lo20 = 0;
    s_acp.shared_ram_sync_gen = 0;
    s_acp.has_shared_ram_ptr = false;
    s_acp.volume = 255;
    s_acp.isMuted = false;
    s_acp.isPaused = false;
    s_acp.output_hz = 8000;
    s_acp.clksPerHostSample = 315000000 / (88 * s_acp.output_hz);
    s_acp.chunk_samples = 256;
    s_acp.buffer_index = 0;

    threadPrepare(&s_audioThread, AudioThreadMain, nullptr,
        &s_audioThreadStack[sizeof(s_audioThreadStack)], MAIN_THREAD_PRIO - 6);
    threadStart(&s_audioThread);
}

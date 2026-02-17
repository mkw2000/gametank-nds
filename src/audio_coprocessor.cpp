#include "SDL_inc.h"
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <cstring>

#include "audio_coprocessor.h"
#include "emulator_config.h"
#ifdef NDS_BUILD
#include <calico/nds/pxi.h>
#endif

#ifdef NDS_BUILD
static inline uint16_t ACP_NextQueueIndex(uint16_t idx) {
    return (uint16_t)((idx + 1) & (ACPState::NDS_IPC_QUEUE_SIZE - 1));
}

static inline uint8_t ACP_ClampVolumeU8(int volume) {
    if (volume < 0) return 0;
    if (volume > 255) return 255;
    return (uint8_t)volume;
}

static inline void ACP_QueuePush(ACPState* state, uint32_t msg) {
    uint16_t nextHead = ACP_NextQueueIndex(state->nds_ipc_head);
    if (nextHead == state->nds_ipc_tail) {
        if (state->nds_remote_ready) {
            uint32_t old = state->nds_ipc_queue[state->nds_ipc_tail];
            state->nds_ipc_tail = ACP_NextQueueIndex(state->nds_ipc_tail);
            pxiSend((PxiChannel)NDS_ACP_PXI_CHANNEL, old);
        } else {
            state->nds_ipc_tail = ACP_NextQueueIndex(state->nds_ipc_tail);
            state->nds_ipc_dropped++;
        }
    }
    state->nds_ipc_queue[state->nds_ipc_head] = msg;
    state->nds_ipc_head = nextHead;
}
#endif

void AudioCoprocessor::ram_write(uint16_t address, uint8_t value) {
	state.ram[address & 0xFFF] = value;
#ifdef NDS_BUILD
    ACP_QueuePush(&state, ndsAcpPackMsg(NDS_ACP_CMD_RAM_WRITE, address & 0x0FFF, value));
#endif
}

uint8_t AudioCoprocessor::ram_read(uint16_t address) {
	return state.ram[address & 0xFFF];
}

void AudioCoprocessor::register_write(uint16_t address, uint8_t value) {
    //printf("audio register %x written with %x\n", (address), value);
	switch(address & 7) {
		case ACP_RESET:
			state.resetting = true;
            state.irqCounter = 255;
            break;
		case ACP_NMI:
#ifdef NDS_BUILD
            // ARM7 offload handles ACP_NMI behavior.
#else
            SDL_LockAudioDevice(state.device);
            state.cpu->NMI();
            state.cpu->RunOptimized(state.cycles_per_sample, state.cycle_counter);
#ifdef WRAPPER_MODE
            state.cpu->RunOptimized(state.cycles_per_sample, state.cycle_counter);
#endif
            SDL_UnlockAudioDevice(state.device);
#endif
			break;
		case ACP_RATE:
			state.irqRate = (((value << 1) & 0xFE) | (value & 1));
            state.running = (value & 0x80) != 0;
            state.cycles_per_sample = state.irqRate * state.clkMult;
            if(state.clksPerHostSample != 0) {
                state.samples_per_frame = (315000000 / 88 / 60) / state.clksPerHostSample;
                if (state.samples_per_frame == 0) {
                    state.samples_per_frame = 1;
                }
                if (state.samples_per_frame > 512) {
                    state.samples_per_frame = 512;
                }
            }
            break;
		default:
			break;
	}
#ifdef NDS_BUILD
    ACP_QueuePush(&state, ndsAcpPackMsg(NDS_ACP_CMD_REG_WRITE, address & 7, value));
#endif
}

void ITCM_CODE AudioCoprocessor::fill_audio(void *udata, uint8_t *stream, int len) {
    ACPState *state = (ACPState*) udata;
    int16_t *stream16 = (int16_t*) stream;

    // If emulation is paused, just fill buffer with zeroes without advancing the apu
    if (state->isEmulationPaused) {
	for(int i = 0; i < len/(int)sizeof(int16_t); i++) {
	    if(stream16 != NULL) {
		stream16[i] = 0;
	    }
	}

	return;
    }

    for(int i = 0; i < len/(int)sizeof(int16_t); i++) {
        if(stream16 != NULL) {
            int sample = ((int)state->dacReg - 128) * state->volume;
            if(state->isMuted) {
                sample = 0;
            } else {
                if(sample > 32767) sample = 32767;
                if(sample < -32768) sample = -32768;
            }
            stream16[i] = (int16_t)sample;
        }
        state->irqCounter -= state->clksPerHostSample;
        if(state->irqCounter < 0) {
            if(state->resetting) {
                state->resetting = false;
                if(state->cpu) {
                    state->cpu->Reset();
                }
            }
            state->irqCounter += state->irqRate;
            state->cycle_counter = 0;
            if(state->running && state->cpu) {
#ifdef NDS_BUILD
                // Keep IRQ cadence exact, but batch CPU execution to reduce ARM9 overhead.
                state->cpu->IRQ();
                state->cpu->ClearIRQ();
                state->nds_pending_cycles += (uint32_t)state->cycles_per_sample;
                state->audio_cycle_accum++;

                if(state->audio_cycle_accum >= state->audio_cycle_divider) {
                    state->audio_cycle_accum = 0;
                    state->cpu->RunOptimized((int32_t)state->nds_pending_cycles, state->cycle_counter);
                    state->nds_pending_cycles = 0;
                }
#else
                state->cpu->IRQ();
                state->cpu->ClearIRQ();
                state->cpu->RunOptimized(state->cycles_per_sample, state->cycle_counter);
#endif
            }
        }
    }

#ifdef NDS_BUILD
    // Drain any remainder so interrupt responses don't get stuck across buffers.
    if(state->running && state->nds_pending_cycles) {
        state->cpu->RunOptimized((int32_t)state->nds_pending_cycles, state->cycle_counter);
        state->nds_pending_cycles = 0;
    }
#endif
}

ACPState* AudioCoprocessor::singleton_acp_state;

#ifdef NDS_BUILD
void AudioCoprocessor::TickNDSAudio() {
    if (!state.nds_remote_ready) {
        return;
    }

    // Forward runtime controls (volume/mute/pause) to ARM7 when changed.
    if (state.nds_last_sent_volume != state.volume) {
        state.nds_last_sent_volume = state.volume;
        ACP_QueuePush(&state, ndsAcpPackMsg(NDS_ACP_CMD_CONTROL, NDS_ACP_CTRL_VOLUME, ACP_ClampVolumeU8(state.volume)));
    }
    if (state.nds_last_sent_muted != state.isMuted) {
        state.nds_last_sent_muted = state.isMuted;
        ACP_QueuePush(&state, ndsAcpPackMsg(NDS_ACP_CMD_CONTROL, NDS_ACP_CTRL_MUTE, state.isMuted ? 1 : 0));
    }
    if (state.nds_last_sent_paused != state.isEmulationPaused) {
        state.nds_last_sent_paused = state.isEmulationPaused;
        ACP_QueuePush(&state, ndsAcpPackMsg(NDS_ACP_CMD_CONTROL, NDS_ACP_CTRL_PAUSE, state.isEmulationPaused ? 1 : 0));
    }

    // Flush bounded number of queued messages to keep frame time stable.
    int sent = 0;
    while ((state.nds_ipc_tail != state.nds_ipc_head) && (sent < 256)) {
        uint32_t msg = state.nds_ipc_queue[state.nds_ipc_tail];
        state.nds_ipc_tail = ACP_NextQueueIndex(state.nds_ipc_tail);
        pxiSend((PxiChannel)NDS_ACP_PXI_CHANNEL, msg);
        ++sent;
    }
}
#endif

__attribute__((always_inline)) inline uint8_t ACP_MemoryRead(uint16_t address) {
    return AudioCoprocessor::singleton_acp_state->ram[address & 0xFFF];
}

__attribute__((always_inline)) inline uint8_t ACP_CPUSync(uint16_t address) {
    uint8_t opcode = ACP_MemoryRead(address);
    if(opcode == 0x40) {
        //If opcode is ReTurn from Interrupt
        AudioCoprocessor::singleton_acp_state->last_irq_cycles = AudioCoprocessor::singleton_acp_state->cycle_counter;
    }
    return opcode;
}

__attribute__((always_inline)) inline void ACP_MemoryWrite(uint16_t address, uint8_t value) {
    AudioCoprocessor::singleton_acp_state->ram[address & 0xFFF] = value;
    if(address & 0x8000) {
        AudioCoprocessor::singleton_acp_state->dacReg = value;
    }
}

const char* AudioFormatString(SDL_AudioFormat f) {
    switch(f) {
        case AUDIO_S8: return "AUDIO_S8";
        case AUDIO_U8: return "AUDIO_U8";
        case AUDIO_S16LSB: return "AUDIO_S16LSB";
        case AUDIO_S16MSB: return "AUDIO_S16MSB";
        //case AUDIO_S16SYS: return "AUDIO_S16SYS";
        case AUDIO_U16LSB: return "AUDIO_U16LSB";
        case AUDIO_U16MSB: return "AUDIO_U16MSB";
        //case AUDIO_U16SYS: return "AUDIO_U16SYS";
        case AUDIO_S32LSB: return "AUDIO_S32LSB";
        case AUDIO_S32MSB: return "AUDIO_S32MSB";
        //case AUDIO_S32SYS: return "AUDIO_S32SYS";
        case AUDIO_F32LSB: return "AUDIO_F32LSB";
        case AUDIO_F32MSB: return "AUDIO_F32MSB";
        //case AUDIO_F32SYS: return "AUDIO_F32SYS";
        default: return "UNKNOWN";
    }
}

void ACP_CPUStopped() {
}

void AudioCoprocessor::StartAudio() {
#ifdef NDS_BUILD
    state.nds_ipc_head = 0;
    state.nds_ipc_tail = 0;
    state.nds_ipc_dropped = 0;
    state.nds_remote_ready = false;
    state.nds_last_sent_volume = -1;
    state.nds_last_sent_muted = !state.isMuted;
    state.nds_last_sent_paused = !state.isEmulationPaused;

    // Wait until ARM7 side has installed a handler/mailbox for this channel.
    pxiWaitRemote((PxiChannel)NDS_ACP_PXI_CHANNEL);
    state.nds_remote_ready = true;

    // Force initial synchronization of basic controls.
    ACP_QueuePush(&state, ndsAcpPackMsg(NDS_ACP_CMD_CONTROL, NDS_ACP_CTRL_VOLUME, ACP_ClampVolumeU8(state.volume)));
    ACP_QueuePush(&state, ndsAcpPackMsg(NDS_ACP_CMD_CONTROL, NDS_ACP_CTRL_MUTE, state.isMuted ? 1 : 0));
    ACP_QueuePush(&state, ndsAcpPackMsg(NDS_ACP_CMD_CONTROL, NDS_ACP_CTRL_PAUSE, state.isEmulationPaused ? 1 : 0));

    // Reset ARM7 ACP state at startup.
    ACP_QueuePush(&state, ndsAcpPackMsg(NDS_ACP_CMD_REG_WRITE, ACP_RESET, 0));
    TickNDSAudio();

    state.format = AUDIO_S16LSB;
    state.device = 1; // dummy non-zero value
#else
    SDL_AudioSpec wanted, obtained;

    /* Set the audio format */
    wanted.freq = 44100;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 1;    /* 1 = mono, 2 = stereo */
    wanted.samples = 512;  /* Good low-latency value for callback */
    wanted.callback = fill_audio;
    wanted.userdata = &state;

    SDL_InitSubSystem(SDL_INIT_AUDIO);

    state.device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);

    /* Open the audio device, forcing the desired format */
    if (state.device  == 0 ) {
        fprintf(stdout, "Couldn't open audio: %s\n", SDL_GetError());
    } else {
        printf("Opened audio device:\n\tFreq: %d\n\tFormat %s\n\tChannels: %d\n\tSamples: %d\n",
            obtained.freq, AudioFormatString(obtained.format), obtained.channels, obtained.samples);
        state.format = obtained.format;
        SDL_PauseAudioDevice(state.device, 0);

        state.clksPerHostSample = 315000000 / (88 * obtained.freq);
    }
#endif
}

AudioCoprocessor::AudioCoprocessor() {
	AudioCoprocessor::singleton_acp_state = &state;

#ifdef NDS_BUILD
    state.cpu = NULL;
#else
    state.cpu = new mos6502(ACP_MemoryRead, ACP_MemoryWrite, ACP_CPUStopped, ACP_CPUSync);
#endif

    state.irqCounter = 0;
    state.irqRate = 0;
    state.resetting = false;
    state.running = false;
    state.clksPerHostSample = 0;
    state.cycles_per_sample = 1024;
    state.samples_per_frame = 367;
    state.last_irq_cycles = 0;
    state.volume = 255;
    state.isMuted = false;
    state.isEmulationPaused = false;
#ifdef NDS_BUILD
    state.audio_cycle_divider = 1;
    state.nds_channel = -1;
    state.nds_buffer_index = 0;
    state.nds_output_hz = 0;
    state.nds_chunk_samples = 0;
    state.nds_frames_until_refill = 0;
    state.nds_pending_cycles = 0;
    state.nds_ipc_head = 0;
    state.nds_ipc_tail = 0;
    state.nds_ipc_dropped = 0;
    state.nds_remote_ready = false;
    state.nds_last_sent_volume = -1;
    state.nds_last_sent_muted = false;
    state.nds_last_sent_paused = false;
#else
    state.audio_cycle_divider = 1;  // Full rate on desktop
#endif
    state.audio_cycle_accum = 0;
    state.clkMult = 4;

#ifdef NDS_BUILD
    memset(state.ram, 0, AUDIO_RAM_SIZE);
#else
	for(int i = 0; i < AUDIO_RAM_SIZE; i ++) {
		state.ram[i] = rand() % 256;
	}
#endif

    if(!EmulatorConfig::noSound) {
        StartAudio();
    } else {
        state.clksPerHostSample = 1024;
    }

	return;
}

void AudioCoprocessor::dump_ram(const char* filename) {
    ofstream dumpfile (filename, ios::out | ios::binary);
    dumpfile.write((char*) state.ram, 4096);
    dumpfile.close();
}

uint16_t AudioCoprocessor::get_irq_cycle_count() {
    return state.last_irq_cycles;
}

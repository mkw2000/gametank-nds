using namespace std;

#include "mos6502/mos6502.h"
#include "SDL_inc.h"
#ifdef NDS_BUILD
#include "nds_acp_ipc.h"
#endif

#define ACP_RESET 0
#define ACP_NMI 1
#define ACP_RATE 6

#define AUDIO_RAM_SIZE 4096

typedef struct ACPState {
	uint8_t ram[AUDIO_RAM_SIZE];
	mos6502 *cpu;
    int16_t irqCounter;
    uint8_t irqRate;
	bool running;
    bool resetting;
    uint8_t dacReg;
    uint16_t clksPerHostSample;
    uint64_t cycles_per_sample;
	uint32_t samples_per_frame;
	uint8_t clkMult;
	SDL_AudioFormat format;
	uint16_t last_irq_cycles;
	uint64_t cycle_counter;
	SDL_AudioDeviceID device;
	int volume;
	bool isMuted;
	bool isEmulationPaused;
	// NDS: Run audio CPU at reduced rate (every Nth sample)
	uint8_t audio_cycle_divider;
	uint8_t audio_cycle_accum;
#ifdef NDS_BUILD
	int nds_channel;
	uint8_t nds_buffer_index;
	uint16_t nds_output_hz;
	uint16_t nds_chunk_samples;
	uint8_t nds_frames_until_refill;
	uint32_t nds_pending_cycles;
	static const uint16_t NDS_IPC_QUEUE_SIZE = 8192;
	uint32_t nds_ipc_queue[NDS_IPC_QUEUE_SIZE];
	uint16_t nds_ipc_head;
	uint16_t nds_ipc_tail;
	uint32_t nds_ipc_dropped;
	bool nds_remote_ready;
	int nds_last_sent_volume;
	bool nds_last_sent_muted;
	bool nds_last_sent_paused;
	int16_t nds_stream_buffers[2][512];
#endif
} ACPState;

class AudioCoprocessor {
private:
	//emulated registers/memory
	ACPState state;
	void capture_snapshot();
public:
	static ACPState* singleton_acp_state;
	AudioCoprocessor();
	void StartAudio();
	void ram_write(uint16_t address, uint8_t value);
	uint8_t ram_read(uint16_t address);
	void register_write(uint16_t address, uint8_t value);
	void dump_ram(const char* filename);
	uint16_t get_irq_cycle_count();
	static void fill_audio(void *udata, uint8_t *stream, int len);
#ifdef NDS_BUILD
	void TickNDSAudio();
#endif
};

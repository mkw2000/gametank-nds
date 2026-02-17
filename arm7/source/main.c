#include <nds.h>
#include <calico.h>

extern void gtAudioOffloadInit(void);

int main(void) {
	// Read user settings from NVRAM.
	envReadNvramSettings();

	// ARM7 platform services expected by ARM9-side libnds/calico APIs.
	keypadStartExtServer();
	lcdSetIrqMask(DISPSTAT_IE_ALL, DISPSTAT_IE_VBLANK);
	irqEnable(IRQ_VBLANK);
	rtcInit();
	rtcSyncTime();
	pmInit();
	blkInit();
	touchInit();
	touchStartServer(80, MAIN_THREAD_PRIO);

	// GameTank ACP offload (ARM7-side 6502 + audio mixer).
	gtAudioOffloadInit();

	wlmgrStartServer(MAIN_THREAD_PRIO - 8);

	while (pmMainLoop()) {
		threadWaitForVBlank();
	}

	return 0;
}

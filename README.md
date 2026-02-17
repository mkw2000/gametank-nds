# gametank-nds

Nintendo DS port/fork of the GameTank emulator, focused on running on real NDS hardware with direct libnds-based rendering and ARM7 audio offload.

## Current Status

- Boots and runs on NDS.
- Current measured performance is roughly ~35% realtime in representative tests.
- Main bottleneck is ARM9 CPU emulation.
- Audio works but still has a repetitive pulse artifact.

## Requirements

- devkitPro with `devkitARM`
- `libnds` / Calico toolchain
- `make`

Environment example:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
```

## Build

```bash
make -j4
```

Output ROM:

- `gametank-nds.nds`

## Run

Launch `gametank-nds.nds` on hardware/flashcart or emulator.

ROM loading:
- If no ROM path is provided, NDS file browser menu opens.
- `.gtr` files are expected.

## NDS Controls (Current)

- Menu navigation: `D-Pad`, `A` select, `B` back/close
- Reset: `SELECT`

Note: Runtime menu behavior is still evolving during optimization work.

## Performance/Profiler Overlay

Bottom screen currently shows:

- Frame timing:
  - `Perf: <us> <realtime%>`
  - `CPU / BLIT / REN / AUD / IN` percentages
- Opcode hotspot sample:
  - `OP1 xx yy% OP2 xx yy%`
  - `OP3 xx yy% E:a/b/c`

Where:
- `xx` = opcode hex
- `yy%` = share of CPU cycles in the sample window
- `E:` = execution counts for top 3 opcodes in the same window

This overlay is the primary tool for data-driven CPU optimization.

## Project Layout

- ARM9 main emulation:
  - `src/gte.cpp`
  - `src/mos6502/mos6502.cpp`
  - `src/blitter.cpp`
- ARM7 audio offload:
  - `arm7/source/audio_offload.cpp`
- IPC/control:
  - `src/audio_coprocessor.cpp`
  - `src/nds_acp_ipc.h`

## Known Issues

- Performance far from full speed.
- Audio pulse artifact.
- Optimization work is ongoing and may temporarily regress behavior between iterations.

## Contributor Notes

Read `AGENTS.md` before making optimization changes. It documents:
- known-good baseline
- regressions already encountered
- safe optimization workflow
- files to touch for CPU/audio work


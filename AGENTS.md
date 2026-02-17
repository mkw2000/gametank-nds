# GameTank NDS Agent Notes

This file documents the current project state, what has already been tried, and a safe process for future optimization passes.

## Goal

Primary goal is Nintendo DS performance. Desktop parity is not required for NDS-specific paths.

Current user-observed status:
- Emulator is stable again.
- Approximate runtime is around 35% realtime.
- CPU is still dominant.
- Audio still has a repetitive pulse artifact.

## Build / Run

- Build command:
  - `make -j4`
- Output ROM:
  - `gametank-nds.nds`

## High-level Architecture (NDS path)

- ARM9:
  - Main emulation loop (`src/gte.cpp`).
  - Main CPU core (`src/mos6502/mos6502.cpp`).
  - Blitter and VRAM conversion (`src/blitter.cpp`).
  - Sends audio control/register messages to ARM7 (`src/audio_coprocessor.cpp`).
- ARM7:
  - Audio synthesis/offload thread (`arm7/source/audio_offload.cpp`).

## Current Instrumentation

### Frame-level perf (ARM9)

In `src/gte.cpp`, bottom-screen perf output includes:
- `Perf: <us> <realtime%>`
- `CPU/BLIT/REN/AUD/IN` split

### Opcode hotspot profiling (ARM9)

In `src/mos6502/mos6502.cpp` + `src/gte.cpp`:
- Per-opcode execution/cycle counters are collected.
- Bottom screen now also prints:
  - `OP1 xx yy% OP2 xx yy%`
  - `OP3 xx yy% E:a/b/c`

Use this to choose optimization targets by actual cycle share.

## Current Known-Good Baseline

The following are currently active and considered safe:
- Direct RGB555 pipeline and direct VRAM path.
- ARM7 audio offload baseline (pre-streaming experiment).
- ARM9 direct opcode switch dispatch using generated include:
  - `src/mos6502/mos6502_dispatch_cases.inc`
- ARM9 `ReadBus/WriteBus` fast path for ROM/RAM with fallback for other regions.
- Cycle count batching with flush-before-slow-bus-access:
  - Avoids timing breakage seen in earlier naive batching.

## Regressions Already Encountered (Do Not Repeat)

1. Incomplete opcode switch
- A partial switch caused illegal opcodes and ROM startup failures.
- Fix was restoring complete opcode coverage.

2. Aggressive ARM9 hot loop
- A do/while fast loop caused black screen, duplicate perf prints, bad PC state.
- Reverted.

3. Naive local cycleCount batching
- Delayed cycle publication caused blitter timing drift and persistent trails.
- Fixed by flushing pending cycles before callback-based bus accesses.

4. ARM7 audio streaming rewrite
- Continuous channel polling/yield version caused non-responsive/glitched boot.
- Reverted to previous threadSleep-based loop.

5. ITCM / memory overflows
- Large dispatch bodies in ITCM or ARM7 path can overflow sections.
- Keep large logic out of ITCM unless measured and bounded.

## Files Most Relevant For CPU Work

- `src/mos6502/mos6502.cpp`
- `src/mos6502/mos6502.h`
- `src/mos6502/mos6502_dispatch_cases.inc`
- `src/gte.cpp` (perf/opcode display)

## Files Most Relevant For Audio Work

- `src/audio_coprocessor.cpp`
- `src/audio_coprocessor.h`
- `src/nds_acp_ipc.h`
- `arm7/source/audio_offload.cpp`

## Recommended Optimization Workflow (Required)

1. Measure first
- Run a representative ROM and capture:
  - perf line
  - CPU/BLIT/REN/AUD/IN
  - OP1/OP2/OP3 lines

2. Optimize only top opcode(s)
- Touch only hottest opcodes by cycle share first.
- Keep behavior identical (flags, page crossing, timing).

3. Rebuild and validate
- Build `make -j4`.
- Test at least:
  - hello-world bouncing square (render correctness, no trails)
  - one game ROM with audio

4. Compare metrics
- Confirm realtime% and CPU% moved in expected direction.
- If regression appears, revert that specific change immediately.

## Practical Guardrails

- Keep changes small and isolated.
- Never merge speed changes without immediate on-device validation.
- Prefer correctness-preserving inlining over architectural rewrites.
- Avoid simultaneous CPU + audio rewrites in one pass.

## Open Problems

1. CPU throughput still far from full speed.
2. Audio pulse artifact remains.
3. Need iterative hotspot-driven opcode optimization on ARM9.


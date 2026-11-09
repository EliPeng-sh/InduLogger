# Validation evidence index

Policy: nothing in this repo counts as "done" without hardware evidence or a test
that could fail. This page indexes each claim with its result and the exact way it
was produced. Nothing here is simulated — pending items are marked as pending.

## M1 — STM32 skeleton (in progress)

| # | Claim | Result | How it was produced |
|---|-------|--------|--------------------|
| 1 | Boots at 100 MHz, banner over the ST-LINK VCP | ✅ | Serial capture: `InduLogger boot v0.1.0-m1` (2026-11-04) |
| 2 | Reset-cause decode, IWDG watchdog actually fires | ✅ | Forced-watchdog run: feed commented out → `reset cause: iwdg` repeating ≈2 s apart ×10; feed restored → stable. Flash-session caveat documented in `docs/notes/flash.md` |
| 3 | I2C accesses bounded (≤100 ms), every failure counted | ✅ (no-sensor half) | No sensor attached: `BME280 init failed (err cnt +2 per 2 s retry)` for minutes, zero resets; DWT reports 51 557 µs per failed attempt, stable across 11 samples |
| 4 | Sensor plausibility logic | ✅ | Host TDD via CTest: 2 suites green (`test_reset_cause`, `test_sensor_check`); CI runs the same tests |
| 5 | MPU6050 driver with last-good cache on out-of-range | ✅ (code) | Not yet exercised on a real chip — pending hardware |
| 6 | DWT cycle counter + bench-GPIO instrumentation | ✅ (part) | I2C attempt latency measured by DWT (51.6 ms, stable); `BENCH_GPIO1` (PA1) frames every attempt for a scope; ±10 % cross-check pending an instrument |
| 7 | CI green on every push (host tests + firmware build + static analysis) | ✅ (through `6aaa543`) | GitHub Actions: 3 jobs green. A later run (`14620c6`) was delayed by a runner-side apt stall — infrastructure, not code (the same Makefile builds locally with zero warnings) |

### Pending (hardware on order)

- BME280 connected readings (T/P/H sane at room conditions) + stack watermark ≥ 40 words
- MPU6050 flat ≈ 1 g / tilted 90° / cable-pull cases
- SD card: mount + write/readback; pull-while-writing tolerance
- Three-task pipeline (Sensor → Queue → Logger) with a watchdog-monitored Monitor task
- DWT-vs-scope cross-check ± 10 % on `BENCH_GPIO1`

## Later milestones

W4 examples: interrupt latency & jitter under stress, power-cut torture, 7-day soak
(logger format planned). Methodology lands here with each milestone.

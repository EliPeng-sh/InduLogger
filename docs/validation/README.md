# Validation evidence index

Policy: nothing in this repo counts as "done" without hardware evidence or a test
that could fail. This page indexes each claim with its result and the exact way it
was produced. Nothing here is simulated — pending items are marked as pending.

## M1 — STM32 skeleton (**complete 2026-11-19**, tag `m1-skeleton`)

| # | Claim | Result | How it was produced |
|---|-------|--------|--------------------|
| 1 | Boots at 100 MHz, banner over the ST-LINK VCP | ✅ | Serial capture: `InduLogger boot v0.1.0-m1` (2026-11-04); repeated across sessions |
| 2 | Reset-cause decode, IWDG watchdog actually fires | ✅ | Forced-watchdog run: feed commented out → `reset cause: iwdg` repeating ≈2 s apart ×10; feed restored → stable. T9 raises the bar: hung-Logger injection → 5 deterministic resets ≈3 s, recovery zero-reset |
| 3 | I2C accesses bounded (≤100 ms), every failure counted | ✅ | No-sensor case: +2 errors per 2 s for minutes, zero resets, 51.6 ms per failed attempt (DWT). Connected case: 275–278 µs per read. Pull-wire + stuck-slave wedge → GPIO bus-clear (9×SCL+STOP) → auto-recovery ≤4 s |
| 4 | Sensor plausibility logic + BME280 compensation correctness | ✅ | Host TDD via CTest (7 suites, incl. `test_sensor_check`, `test_bme280_compensate` vs Bosch datasheet vectors); the compensation suite caught a real ×2.56 pressure bug before board bring-up — see `docs/reports/t6-pressure-compensation-bug.md` |
| 5 | MPU6050 driver + board-level physical evidence | ✅ | WHO_AM_I whitelist {0x68, 0x70} host-TDD (bench die is an MPU6500); board: flat az=+1.06 g, 90° tilt axis swap +X→−Y at 1.013 g, cable-pull bounded failures |
| 6 | SD card + FATFS: mount/write/readback with fault injection | ✅ | Board: `sd: mounted, readback: boot,ok`; pull-card → auto-remount ≤4 s (two-layer invalidation: glue flag + ST `disk.is_initialized[]` cache — see `docs/reports/t8-st-diskio-cache-bug.md`); NRST mid-write ×3 → remount every boot |
| 7 | Three-task pipeline + alive-mask watchdog under stress | ✅ | Host TDD (`test_csv_format`, `test_wd_alive`); board: 10 ms sampling saturates the queue → drop-newest (`qdrop` climbs), zero resets; four-fix chain documented in `docs/reports/t9-three-task-watchdog.md` |
| 8 | DWT timing credible (instrument-grade) | ✅ | Scope cross-check on `BENCH_GPIO1` (PB0/A3): 277 µs measured vs 277 µs DWT = 0 % deviation (±10 % criterion); calibration square wave validated the probe chain first — `docs/reports/t10-dwt-scope-crosscheck.md` |
| 9 | CI green on every push (host tests + firmware build + static analysis) | ✅ | GitHub Actions: 3 jobs green on every task commit through the M1 tag |

### Known non-blocking items

- Old SD card (FAT16) has a corrupted `indulog.csv` directory chain — firmware
  auto-switched to the fallback file `indulog2.csv` (board-verified). Card recovery
  (chkdsk/reformat) deferred; it exercises the fallback path as designed.

## Later milestones

W4 examples: interrupt latency & jitter under stress, power-cut torture, 7-day soak
(logger format planned). Methodology lands here with each milestone.

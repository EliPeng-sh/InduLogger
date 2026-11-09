# InduLogger

> Product-grade STM32 + FreeRTOS industrial data-logging platform with an ESP32-S3
> connectivity co-processor (Wi-Fi / BLE / MQTT) and secure FOTA with power-fail
> recovery and automatic rollback.

**Measured, not claimed**: every feature is verified on hardware or by a test that
could fail before it counts — see the evidence index in
[`docs/validation/`](docs/validation/README.md).

[![CI](https://github.com/EliPeng-sh/InduLogger/actions/workflows/ci.yml/badge.svg)](https://github.com/EliPeng-sh/InduLogger/actions/workflows/ci.yml)
— host unit tests, firmware build, and static analysis on every push.

## Status — milestone M1 (STM32 skeleton, in progress)

| Area | State |
|---|---|
| Headless Makefile build + `make flash` (CubeProgrammer CLI), LD2 heartbeat at 100 MHz | ✅ done |
| Debug console: `printf` over the ST-LINK VCP, boot banner | ✅ done |
| Diagnostics: reset-cause decode (host-TDD), IWDG watchdog live, per-error counters | ✅ done |
| BME280 (T/P/H) — bounded I²C (100 ms), calibration, error counters | code done — connected-sensor reading pending hardware |
| MPU6050 — plausibility checks (host-TDD), last-good cache, clone-ID guard | code done — on-chip cases pending hardware |
| DWT cycle counter + bench-GPIO instrumentation | ✅ done (I²C latency measured) |
| SD card + FATFS (mount/write/readback, dirty-card tolerant) | next (hardware on order) |
| Three-task pipeline (Sensor → Queue → Logger) + watchdog Monitor | planned |
| CI: host tests + firmware build + cppcheck | ✅ green |

Milestone plan with per-task checkboxes (the progress ledger):
[`docs/plans/2026-10-29-m1-stm32-skeleton.md`](docs/plans/2026-10-29-m1-stm32-skeleton.md)

## Documentation map

| Doc | Contents |
|---|---|
| [`docs/specs/2026-10-27-indulogger-design.md`](docs/specs/2026-10-27-indulogger-design.md) | Design specification (Chinese): architecture, layers, requirements, acceptance claims |
| [`docs/plans/2026-10-29-m1-stm32-skeleton.md`](docs/plans/2026-10-29-m1-stm32-skeleton.md) | M1 implementation plan, 11 tasks, checkbox = progress |
| [`docs/validation/README.md`](docs/validation/README.md) | Evidence index — every claim with result and methodology |
| [`docs/notes/toolchain.md`](docs/notes/toolchain.md) | Toolchain versions and repository conventions |
| [`docs/notes/flash.md`](docs/notes/flash.md) | Flashing, debugging, serial-capture workflow |
| [`AGENTS.md`](AGENTS.md) | Engineering handoff notes (environment facts, gotchas) |

## Architecture

```
stm32/            firmware — CubeMX-generated core + hand-written layers
  app/            vertical scenarios (industrial_logger): tasks + pipeline
  platform/       config, console, stats/diagnostics, time, health
  middleware/     Modbus, protocol stacks, FATFS glue        (later milestones)
  bsp/            board-level drivers: bme280, mpu6050, sdcard
host/             PC-side Unity test suites (CTest) — same sources as firmware
.github/          CI: host tests, firmware build, cppcheck (generated trees excluded)
```

Layers are enforced: hand-written code lives under `app/`, `platform/`, `bsp/`;
CubeMX owns `Core/`, `Drivers/`, `Middlewares/`, `FATFS/`.

## Build, flash, test

```bash
# firmware (from the repo root; arm-none-eabi-gcc + repo Makefile)
make -C stm32            # -> stm32/build/InduLogger.elf + .bin (zero warnings)
make -C stm32 flash      # STM32CubeProgrammer CLI over SWD (Windows ST-LINK via interop)

# host unit tests (CMake + Ninja + vendored Unity)
cmake -S host -B host/build -G Ninja && cmake --build host/build && ctest --test-dir host/build
```

## Planned highlights

- STM32F411 + FreeRTOS: sensor sampling, SD-card logging (FATFS), Modbus RTU slave
- ESP32-S3 co-processor: Wi-Fi + MQTT, BLE provisioning, custom UART protocol (CRC16, ACK/retry)
- v2: Secure FOTA — dual-slot bootloader, SHA256 verification, automatic rollback
- Layered "platform" architecture, extensible to instrumentation and medical verticals
- Interrupt latency & jitter under stress, power-cut torture tests, and 7-day soak
  results — published with methodology in `docs/validation/` as milestones complete

Wiring diagrams and demo videos will land here as milestones complete.

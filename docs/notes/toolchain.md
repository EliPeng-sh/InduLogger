# Toolchain

- Hosts: WSL2 Ubuntu 26.04 (docs, host tests, git) + Windows (STM32CubeIDE, flash/debug)
- STM32CubeIDE: Windows native, embeds the CubeMX engine; version **TBD — record at first install** (Help → About)
- Firmware build: CubeIDE managed build — "Build Project" locally, headless build in CI; output `stm32/Debug/InduLogger.elf`
- Bundled arm-none-eabi-gcc: version **TBD — record at first build** (build console banner)
- Flash/debug: CubeIDE built-in ST-LINK (plug-and-play); backup = OpenOCD 0.12.0 (apt) over usbipd-win
- Host tests: host gcc + CMake 4.4.0 + Ninja, Unity v2.6.0 via CMake FetchContent, driven by CTest
- FreeRTOS: CubeIDE-bundled, raw API, heap_4 (30 KB), tick 1 kHz
- Retired 2026-10-30: apt arm-none-eabi-gcc 14.2.1 + CubeMX CMake output (superseded by the CubeIDE migration)

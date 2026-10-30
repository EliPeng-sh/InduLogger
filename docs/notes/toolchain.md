# Toolchain

- Host: WSL2 Ubuntu 26.04
- arm-none-eabi-gcc 14.2.1 (apt 15:14.2.rel1-1)
- CMake 4.4.0, Ninja
- OpenOCD (apt) — flash via ST-Link over usbipd-win
- STM32CubeMX: >= 6.11, CMake output
- Unity v2.6.0 via CMake FetchContent (host tests)
- FreeRTOS: CubeMX-bundled, raw API, heap_4 (30 KB)

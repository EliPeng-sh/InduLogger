# Toolchain

- Hosts: **`D:\InduLogger` = repo root** (NTFS; CubeIDE works natively on the drive-letter path; git/docs/tests run from WSL as `/mnt/d/InduLogger`)
- Firmware build: repo Makefile `stm32/Makefile` + arm-none-eabi-gcc **14.2.1** (Ubuntu apt 15:14.2.rel1-1) — `make` in `stm32/` → `stm32/build/InduLogger.elf`; the same Makefile runs in CI (ubuntu-latest). Warnings: -Wall everywhere, -Wextra on hand-written code (Task 6+)
- (Retired 2026-11-04: CubeIDE managed build on the WSL UNC path — its generated makefiles embed `\\wsl.localhost\...` absolute paths and break make. After moving the repo to `D:\` the CubeIDE Build works too; the Makefile stays the canonical entry for CI parity + zero-warning gating; CubeIDE remains the .ioc code generator and graphical debugger.)
- Flash: `make flash` — STM32CubeProgrammer CLI **v2.22.0** (`C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe`) called from WSL via interop, ST-LINK on Windows USB (no usbipd needed). Backup: OpenOCD 0.12.0 (apt) over usbipd-win
- Host tests: host gcc + CMake 4.4.0 + Ninja, Unity v2.6.0 via CMake FetchContent, driven by CTest
- Code generation: STM32CubeIDE with embedded CubeMX **6.12.1**, STM32Cube FW_F4 **V1.28.3**; generation style: task functions + all MX_* inits inline in main.c (freertos.c = hooks only)
- FreeRTOS: CubeIDE-bundled, CMSIS_V1 wrapper + native API in app code (raw-API option removed in CubeMX ≥6.11), heap_4 (30 KB), tick 1 kHz, `configUSE_NEWLIB_REENTRANT=1`
- FatFs R0.12c: `_FS_TINY=0`, `_USE_LFN=2` (dynamic working buffer on the STACK), `_MAX_LFN=255`

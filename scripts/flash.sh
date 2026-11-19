#!/usr/bin/env bash
# Backup flash path: OpenOCD over usbipd (device must be attached to WSL first —
# see docs/notes/flash.md). Main path is `make flash` (STM32CubeProgrammer CLI).
set -euo pipefail
ELF="${1:-stm32/build/InduLogger.elf}"
exec openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program ${ELF} verify reset exit"

# Flash & debug

Build first: `make -C stm32` → `stm32/build/InduLogger.elf`

## Main: STM32CubeProgrammer CLI (`make flash`)

- From `stm32/`: `make flash` =
  `STM32_Programmer_CLI.exe -c port=SWD -w build/InduLogger.elf -v -g 0x08000000`
- ST-LINK plugs into a Windows USB port; invoked from WSL via interop — **no usbipd needed**
- Toolchain: CubeProgrammer v2.22.0, ST-LINK FW V2J28M18 (SN 066CFF575655898367242010)
- Windows USB selective suspend is disabled (2026-11-04): the old ST-LINK FW wedges after
  a suspend cycle (`DEV_USB_COMM_ERR` on every open until replug); a FW upgrade to V2J39
  via the CubeProgrammer GUI removes that need if it ever returns
- **Expected after every flash: `reset cause: iwdg` on the first boot.** A flash session
  (halt + erase + program, several seconds) outlasts the 2 s IWDG window, so the watchdog
  fires mid-flash; CubeProgrammer re-attaches, verify still passes. Harmless — and a nice
  demonstration that the watchdog is live.

## Serial capture for evidence (`scripts/serial_capture.sh`)

- `scripts/serial_capture.sh [seconds]` (default 30) listens on the ST-LINK VCP
  from WSL via interop (port auto-detected through `Get-PnpDevice`, 115200 8N1)
  and writes `C:\temp\serial_cap.txt` = `/mnt/c/temp/serial_cap.txt`
- **The file appears only after the capture window ends.** Start the listener in
  the background, flash during the window, then read the file:

  ```bash
  scripts/serial_capture.sh 30 &
  make -C stm32 flash
  sleep 5   # let the window finish, then read
  cat /mnt/c/temp/serial_cap.txt
  ```

## Debug: CubeIDE graphical debug

- Debug As → STM32 C/C++ Application, with the debug configuration set to
  **Build Before Launch = disabled** and pointed at the existing `stm32/build/InduLogger.elf`
- **Never build via CubeIDE's managed build** (repo Makefile is canonical — CI parity);
  `DBGMCU IWDG freeze` in main.c keeps breakpoints from tripping the watchdog

## Backup: OpenOCD over usbipd (PowerShell as Admin)

```powershell
usbipd bind --hardware-id 0483:3748
usbipd attach --wsl --hardware-id 0483:3748
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program stm32/build/InduLogger.elf verify reset exit"
```

`scripts/flash.sh` wraps the OpenOCD command (ELF path optional argument).

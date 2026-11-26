#!/usr/bin/env bash
# Capture serial output from a USB-attached COM port for evidence.
# Usage: scripts/serial_capture.sh [seconds] [name-match] [win-outfile]
#   seconds      capture window length                (default 30)
#   name-match   FriendlyName regex selecting the port (default "Virtual COM Port"
#                                                       = ST-LINK VCP on the Nucleo)
#   win-outfile  Windows path for the captured text    (default C:\temp\serial_cap.txt)
#
# Port is auto-detected via Get-PnpDevice (115200 8N1), listener runs from WSL
# via interop. Examples:
#   scripts/serial_capture.sh 30                          # STM32 console (COM13)
#   scripts/serial_capture.sh 30 "CH343" C:\temp\esp_cap.txt   # ESP32-S3 console
#
# The file appears ONLY after the capture window ends. Typical flow:
#   scripts/serial_capture.sh 30 &        # start listener in background
#   make -C stm32 flash                   # flash during the window
#   sleep 5                               # let the window finish
#   cat /mnt/c/temp/serial_cap.txt        # read the captured output
set -euo pipefail

WIN="${1:-30}"
MATCH="${2:-Virtual COM Port}"
OUT="${3:-C:\\temp\\serial_cap.txt}"

# WSL view of the Windows outfile (C:\... -> /mnt/c/...) for the pre-delete.
OUTW="/mnt/$(printf '%s' "$OUT" | tr 'A-Z\\' 'a-z//')"

PS=$(cat <<'EOF'
$port = (Get-PnpDevice -PresentOnly |
  Where-Object { $_.FriendlyName -match "__MATCH__" } |
  ForEach-Object { if ($_.FriendlyName -match "\((COM\d+)\)") { $Matches[1] } } |
  Select-Object -First 1)
if (-not $port) { Write-Error "port matching '__MATCH__' not found"; exit 1 }
$p = New-Object IO.Ports.SerialPort($port, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$p.Open()
$sw = [Diagnostics.Stopwatch]::StartNew(); $buf = ""
while ($sw.Elapsed.TotalSeconds -lt __SECONDS__) { $buf += $p.ReadExisting(); Start-Sleep -Milliseconds 100 }
$p.Close()
Set-Content -Path "__OUT__" -Value $buf
EOF
)

PS="${PS//__SECONDS__/$WIN}"
PS="${PS//__MATCH__/$MATCH}"
PS="${PS//__OUT__/$OUT}"
rm -f "$OUTW"
exec powershell.exe -NoProfile -Command "$PS"

#!/usr/bin/env bash
# Capture ST-LINK VCP serial output for evidence.
# Usage: scripts/serial_capture.sh [seconds]     (default 30)
#
# Starts a PowerShell listener on the ST-LINK Virtual COM Port (port is
# auto-detected via Get-PnpDevice; 115200 8N1) from WSL via interop and
# writes C:\temp\serial_cap.txt = /mnt/c/temp/serial_cap.txt.
#
# The file appears ONLY after the capture window ends. Typical flow:
#   scripts/serial_capture.sh 30 &        # start listener in background
#   make -C stm32 flash                   # flash during the window
#   sleep 5                               # let the window finish
#   cat /mnt/c/temp/serial_cap.txt        # read the captured output
set -euo pipefail

WIN="${1:-30}"

PS=$(cat <<'EOF'
$port = (Get-PnpDevice -PresentOnly |
  Where-Object { $_.FriendlyName -match "Virtual COM Port" } |
  ForEach-Object { if ($_.FriendlyName -match "\((COM\d+)\)") { $Matches[1] } } |
  Select-Object -First 1)
if (-not $port) { Write-Error "ST-LINK VCP not found"; exit 1 }
$p = New-Object IO.Ports.SerialPort($port, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$p.Open()
$sw = [Diagnostics.Stopwatch]::StartNew(); $buf = ""
while ($sw.Elapsed.TotalSeconds -lt __SECONDS__) { $buf += $p.ReadExisting(); Start-Sleep -Milliseconds 100 }
$p.Close()
Set-Content -Path "C:\temp\serial_cap.txt" -Value $buf
EOF
)

PS="${PS//__SECONDS__/$WIN}"
rm -f /mnt/c/temp/serial_cap.txt
exec powershell.exe -NoProfile -Command "$PS"

# M2.2 — UART Link + CommTask + ESP32 Echo — Board Evidence Report

Date: 2026-11-26 · Branch `m2-coproc` · Spec: `docs/specs/2026-11-25-m2-2-uart-link-design.md`
Plan: `docs/plans/2026-11-25-m2-2-uart-link.md` · Status: acceptance #1-#3 ✅, #4-#7 pending (Task 8/9)

## 1. Environment record

| Item | Value |
|---|---|
| STM32 | Nucleo-F411RE, firmware HEAD-era build (re-arm + console-'p' fix), USART1 115200 8N1 DMA **CIRCULAR** |
| Console VCP | ST-LINK "Virtual COM Port" — drifted COM13→**COM17** across USB replugs (name-auto-detect unaffected) |
| ESP32 | clone `ESP32-S3 V1.4`, module **N16R8** (16MB flash + 8MB OPI PSRAM, boot-verified), bridge CH343 = COM22 |
| ESP firmware | IDF **v6.0.3** official build (no loopback/suppress flags), UART1 TX=**GPIO13** RX=GPIO12 |
| Wiring | ESP GPIO13→Nucleo RX hole (PA10), Nucleo TX hole (PA9)→ESP GPIO12, GND↔GND, ESP self-powered via USB |
| Capture | `scripts/serial_capture.sh [secs] [name-match] [outfile]` — dual-port (COM17 STM32 / CH343 ESP) |

## 2. Acceptance verdicts

| # | Action | Verdict | Evidence |
|---|---|---|---|
| 1 | Wire up, flash both | ✅ | both banners seen; STM32 `cause=power-on/software` only, no unexpected iwdg across all rounds |
| 2 | STM32 PING (3B) → ACK + echo | ✅ | boot `PING echo seq=0 len=3`; console `p`×3 → `seq=1/2/3`; `tx=7 acked=7 retry=0 fault=0` |
| 3 | Idle 12 s → heartbeats both sides | ✅ | STM32 rx +2/10s and ESP rx +2/10s during idle windows; link UP sustained |
| 4 | ESP suppresses ACK → retry×3 → FAULT/DOWN | ⏳ Task 8 | |
| 5 | Cable pull 30 s → DOWN; replug → UP, no reset | ⏳ Task 8 | |
| 6 | 30 min soak, crc=0 | ⏳ Task 9 | current soak-so-far: crc=0 across all joint windows |
| 7 | LED + no new iwdg throughout | ⏳ Task 9 | no iwdg observed in any joint capture |

## 3. Key excerpts

Acceptance #2 (`C:\temp\stm32_loop.txt`, console 'p' at t=0/6/12 s):

```
[comm] PING echo seq=0 len=3
[comm] PING echo seq=1 len=3
[comm] PING echo seq=2 len=3
[comm] PING echo seq=3 len=3
[comm] tx=6 acked=6 retry=0 fault=0 rx=67 dup=0 crc=0 orr=0 evd=0 rmm=16
[comm] tx=7 acked=7 retry=0 fault=0 rx=71 dup=0 crc=0 orr=0 evd=0 rmm=16
```

Acceptance #3 idle (STM32 + ESP same window):

```
[mon] up=124s cause=software ...   [mon] up=144s ...
[comm] tx=4 acked=4 retry=0 fault=0 rx=33 dup=0 crc=0 orr=0 evd=0 rmm=16
[comm] tx=4 acked=4 retry=0 fault=0 rx=35 dup=0 crc=0 orr=0 evd=0 rmm=16
--- ESP ---
I (10785) comm: tx=0 acked=0 retry=0 fault=0 rx=2 crc=0
I (20785) comm: tx=0 acked=0 retry=0 fault=0 rx=4 crc=0
```

## 4. The RX-deafness saga — root cause and fix (the milestone's biggest find)

Symptom: ESP→STM32 delivered zero frames (`rx` frozen, `evd=0`) in three joint rounds despite
three different wire bodies and a TX-pin move (GPIO11→13). Every element was proven good in
isolation (self-loopback jumper test; Task 2 loopback), yet any disturbance "killed" the wire.

Root cause (`stm32f4xx_hal_uart.c` UART_DMAAbortOnError path): **any** RX error (framing/noise/
overrun — e.g. the line glitch when the ESP resets during flashing or USB replug) makes HAL
**permanently abort the DMA reception**; `uart_link` armed `ReceiveToIdle_DMA` once at boot with
no recovery → STM32 RX deaf until *its own* next reboot. The "intermittent wire" pattern was a
red herring: the route worked from STM32 boot until the first ESP reset event, every time.

Fix (commit `70cb84c`): `HAL_UART_ErrorCallback` sets an ISR-safe flag; `uart_link_read` re-arms
the DMA, resyncs the ring, and counts re-arms — new `rmm=` field in the `[comm]` line.
Regression evidence (board): mid-window ESP RTS-reset → `rmm` climbed to 16-21 and `rx`
**resumed growing** instead of freezing. Field-validated again in §3: `rmm=16` stable, crc=0.

Tooling artifact found alongside: opening COM22 via the CH343 auto-reset circuit can reset the
ESP (DTR/RTS edge-dependent, not every open) — ESP-side captures must account for it.

## 5. Rulings during joint bring-up

| ID | Ruling |
|---|---|
| J1 | ESP link TX moved GPIO11→**GPIO13** and kept: hole-11 was exonerated by the HAL-abort root cause, but re-moving the wire back adds a physical step for zero benefit; GPIO13 is flashed, stable, evidenced. Spec §5 pin note amended accordingly. |
| J2 | console-'p' trigger compares `HAL_OK`, not `1` (plan defect caught by board test, commit `ca9e27d`). |
| J3 | Heartbeat frames do not bump `tx_frames` (session library semantics, verified in `coproc_session.c`): ESP `tx=0` while heartbeats flow is **expected**, not a fault. |

## 6. Pending

Task 8: acceptance #4 (`sdkconfig.suppress.defaults` adversarial build), #5 (cable pull/replug —
now also the natural regression for the re-arm fix). Task 9: #6 30-min soak, #7 LED/iwdg watch,
final verdict table, AGENTS.md pitfall entries (HAL-abort deafness, data_bits-omission,
SDKCONFIG_DEFAULTS cache, CH343 port-open reset, mis-seat red herring, COM drift).

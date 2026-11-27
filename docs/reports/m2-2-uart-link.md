# M2.2 — UART Link + CommTask + ESP32 Echo — Board Evidence Report

Date: 2026-11-27 · Branch `m2-coproc` · Spec: `docs/specs/2026-11-25-m2-2-uart-link-design.md`
Plan: `docs/plans/2026-11-25-m2-2-uart-link.md` · Status: acceptance #1-#7 ✅ — M2.2 COMPLETE

## 1. Environment record

| Item | Value |
|---|---|
| STM32 | Nucleo-F411RE, firmware HEAD-era build (re-arm + re-arm throttle + console-'p' fix), USART1 115200 8N1 DMA **CIRCULAR** |
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
| 4 | ESP suppresses ACK → retry×3 → FAULT/DOWN | ✅ | `p`×3 under SUPPRESS_ACK build: retry climbs 3/frame, fault 1→3; DOWN/UP flapping as heartbeats revive link (§3) |
| 5 | Cable pull 30 s → DOWN; replug → UP, no reset | ✅ | cable4: rx freezes 1952 → `link DOWN` at ≈15 s silence → replug → `link UP`, rx resumes; `rmm` capped +18 (1/s throttle), uptime continuous, no reset (§3) |
| 6 | 30 min soak, crc=0 | ✅ | soak capture: `[mon] up=16774→16809s` (≈4.7 h continuous, no touching since cable4) with `crc=0` throughout and rx steady +2/10s (3337→3343); `rmm=77` frozen (no re-arm events in steady state) |
| 7 | LED + no new iwdg throughout | ✅ | no iwdg in any joint capture (cause=power-on across 4.7 h); LD2 1 Hz heartbeat visually confirmed by user 2026-11-27 |

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

Acceptance #4 (`C:\temp\stm32_loop.txt`, console 'p'×3 while ESP ran the SUPPRESS_ACK build):

```
[comm] PING echo seq=0 len=3
[comm] tx=9 acked=8 retry=3 fault=1 rx=202 dup=0 crc=0 orr=0 evd=0 rmm=153
[comm] link DOWN
[comm] link UP
[comm] tx=11 acked=8 retry=9 fault=3 rx=204 dup=0 crc=0 orr=0 evd=0 rmm=153
[comm] link DOWN
[comm] link UP
```

Each new PING pends, retransmits 3× (retry 3/frame), faults (fault 1→3), drives link DOWN;
ESP heartbeats (still arriving, rx growing) revive it — exactly the host-tested chain on hardware.

Acceptance #5 (cable4 60 s window; pull at 0 s, replug mid-window):

```
[comm] tx=1 acked=0 retry=3 fault=1 rx=1952 dup=0 crc=0 orr=0 evd=0 rmm=65
[comm] link DOWN
[comm] tx=1 acked=0 retry=3 fault=1 rx=1952 dup=0 crc=0 orr=0 evd=0 rmm=75
[comm] link UP
[comm] tx=1 acked=0 retry=3 fault=1 rx=1955 dup=0 crc=0 orr=0 evd=0 rmm=77
[mon] up=9814s ... up=9869s cause=power-on    (real-time continuous, no reset)
```

`rmm` climbed at most +10 per 10 s during the floating phase = the 1/s re-arm throttle cap
(contrast the pre-throttle storm below); `rx` stayed frozen at the real frame count.

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

Hardening follow-up — re-arm storm on a floating line (first cable-pull round): the re-arm-only
binary retried at consumer frequency while PA10 floated (continuous framing errors), and each
re-arm re-exposed stale ring content that re-parsed as valid frames: `rmm` +2034 and `rx` +9626
in one window. Fix: re-arm throttled to 1/s (`xTaskGetTickCount`-based), drain gated on
DMA-confirmed fresh bytes (ISR delta accounting), boot PING moved to a loop-count countdown.
Field proof: §3 cable4 shows the cap signature (+10/10 s max) with `rx` frozen at the true count.

Capture-tooling artifact #2 — VCP backlog replay: the ST-LINK VCP flushes its buffered RX tail
when the port reopens, so the head of a fresh capture can be old-era lines; between rounds the
boards also run unmonitored (counters advance in silence). Two apparent anomalies in cable4 (an
`rx` +1916 jump, an `up=` +9575 s jump) were both this artifact × a 2.6 h gap, not firmware —
the boot timestamp reconciles exactly with live `up=9814 s`. The earlier cable3-era
"HAL_GetTick lazy-poll fast-forward" speculation is superseded: `[mon] up=` reads
`xTaskGetTickCount()`, not `HAL_GetTick()`.

## 5. Rulings during joint bring-up

| ID | Ruling |
|---|---|
| J1 | ESP link TX moved GPIO11→**GPIO13** and kept: hole-11 was exonerated by the HAL-abort root cause, but re-moving the wire back adds a physical step for zero benefit; GPIO13 is flashed, stable, evidenced. Spec §5 pin note amended accordingly. |
| J2 | console-'p' trigger compares `HAL_OK`, not `1` (plan defect caught by board test, commit `ca9e27d`). |
| J3 | Heartbeat frames do not bump `tx_frames` (session library semantics, verified in `coproc_session.c`): ESP `tx=0` while heartbeats flow is **expected**, not a fault. |
| J4 | Acceptance #4 needs actually-pending frames: console `p` was used to generate PINGs under the SUPPRESS_ACK build (an idle window proves nothing — heartbeats are fire-and-forget). |
| J5 | Under ACK suppression the link still flaps DOWN→UP (fault drives DOWN; any legal frame — ESP heartbeats — revives UP): correct semantics, not a second bug. |
| J6 | Re-arm throttled to 1/s + drain gated on fresh bytes + boot PING as loop-count countdown (cable3 storm evidence `rmm` +2034 / `rx` +9626); field signature after fix: `rmm` climbs at most +10 per 10 s. |
| J7 | Stale-ring re-parse mechanism: re-arm reset `rd=0` while event bookkeeping treated the whole stale span as fresh — closed by the fresh-gate + `s_last_wr` reset pairing. |
| J8 | cable4 boot-PING `fault=1` is cosmetic (STM32 booted while ESP/wire not yet settled); heartbeats flowed immediately after. |
| J9 | VCP backlog replay + unmonitored inter-round gaps produce apparent counter jumps; reconcile `up=` against wall-clock boot time before declaring a firmware anomaly. |

## 6. Pending

None — M2.2 complete 2026-11-27 (acceptance 7/7 green; soak 4.7 h `crc=0`; LD2 visually
confirmed; AGENTS.md pitfalls 24-30 recorded). Next milestone: M2.3 WiFi/MQTT, with the
pre-ruled RTC basic init landing before it (ruling R7).

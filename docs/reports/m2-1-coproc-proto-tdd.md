# M2.1 — Coprocessor Protocol Library (host TDD) — Evidence Report

**Date:** 2026-11-23 · **Branch:** `m2-coproc` · **Plan:** `docs/plans/2026-11-20-m2-1-coproc-proto-tdd.md` · **Spec:** `docs/specs/coproc-protocol-spec.md`

## 1. What M2.1 delivers

A pure-C protocol library for the STM32F411 ↔ ESP32-S3 UART link, developed and verified
entirely on the host (the ESP32 board has not arrived), plus build wiring and an English
protocol specification:

| Layer | Files | Responsibility |
|---|---|---|
| Frame | `stm32/middleware/coproc_proto/coproc_proto.{h,c}` | CRC-16/CCITT-False, encoder, byte-at-a-time streaming parser with SOF resynchronisation |
| Session | `stm32/middleware/coproc_proto/coproc_session.{h,c}` | stop-and-wait TX (ACK/deadline/retry/FAULT), RX de-duplication, heartbeat, link UP/DOWN, statistics counters |
| Host tests | `host/tests/test_coproc_proto.c`, `host/tests/test_coproc_session.c` | 23 Unity cases, golden byte vectors, deterministic fuzz |
| Wiring | `host/CMakeLists.txt`, `stm32/Makefile`, `.github/workflows/ci.yml` | library compiled and checked by host tests, firmware image and static analysis |
| Documentation | `docs/specs/coproc-protocol-spec.md` | standalone normative spec (portfolio deliverable, design spec §15) |

Constraints held (plan Global Constraints): C11, no HAL/RTOS include, no `malloc`, no mutable
file-scope state, all timing taken as a `now_ms` parameter, `-Wall -Wextra` clean.

## 2. Claims and the evidence that pins them

| # | Claim | Evidence (command / artifact) | Result |
|---|---|---|---|
| 1 | Frame format, CRC and golden vectors are byte-exact | `ctest --test-dir host/build -R coproc` (tests 1–5; `A5 5A 00 00 00 02 A4 82`, check value `0x29B1`) | 2 suites pass |
| 2 | Parser survives arbitrary chunking, garbage, SOF-lookalike payloads, CRC corruption, oversized LEN | tests 6–11 | pass |
| 3 | Stop-and-wait semantics: 500 ms deadline, 3 retries, identical retransmitted bytes, FAULT + link DOWN after the 4th miss | tests 12–15 | pass |
| 4 | RX de-duplication re-ACKs without re-delivery; unknown CMD → NACK `0x01`; 15 s silence → DOWN, any valid frame → UP; heartbeat at 5 s | tests 16–20 | pass |
| 5 | `now_ms` rollover does not cause a premature timeout | test 21 (`send` at `0xFFFFFF00`, `tick` at `0xFFFFFF10` emits nothing, retransmit lands exactly at the wrapped `0xF4`) | pass (was failing before the fix — see §4) |
| 6 | Stale / foreign / malformed ACK cannot confirm the wrong pending frame | test 22; verified non-vacuous by mutation (removing either half of the ACK guard makes test 22 fail) | pass |
| 7 | `SEQ` wrap 255 → 0 is a new frame, not a duplicate | test 23 | pass |
| 8 | Random byte noise cannot crash or wedge the session | test 18 (seeded xorshift, 1 KB in 7-byte chunks, then normal traffic resumes) | pass |
| 9 | Firmware still builds with the library linked in | `TMPDIR=/home/eli_peng/tmp make -C stm32 -j4` | clean, zero warnings |
| 10 | Static analysis green, including MISRA C:2012 add-on on the new code | the exact ci.yml cppcheck command run locally; `cppcheck --addon=misra.py … stm32/middleware` | both exit 0, 0 findings |
| 11 | CI agrees | GitHub Actions run for `f604662`: host-tests / firmware-build / static-analysis | 3/3 success |
| 12 | The written spec does not claim more than the code does | spec §2–§9 cross-checked against implementation; §6/§7 heartbeat wording corrected (it also fires while a frame awaits its ACK) | consistent |

Full local suite at closeout: **9/9 test executables pass** (7 from M1 + 2 protocol suites,
23 protocol cases).

## 3. Memory and timing footprint

Measured on the host build (`sizeof`, 32-bit target identical layout):

| Object | Size |
|---|---|
| `cp_parser_t` | 524 B |
| `cp_frame_t` | 516 B |
| `cp_session_t` (includes one parser + one pending frame) | **1100 B** |

Static allocation only; sized comfortably against the F411's 128 KB SRAM and the M2.2
CommTask stack budget. `CP_FRAME_MAX` = 520 B is the largest transmit buffer a caller must
provide (documented on each API).

## 4. Final whole-branch review (`d476d8b..f604662`)

Reviewer verdict: *ready with fixes*, no Critical findings.

| Finding | Grade | Disposition |
|---|---|---|
| `cp_session_tick` compared `now_ms >= deadline_ms` — not rollover-safe; a frame sent within 500 ms of the `uint32_t` wrap (every ~49.7 days) times out immediately, retransmits on every tick and reports a spurious FAULT + `link = DOWN` | Important | **Fixed**: signed-difference comparison `(int32_t)(now_ms - s->deadline_ms) >= 0`; regression test 21; rule made normative in protocol spec §6 so the ESP32 side does not repeat it |
| Frozen behaviour "stale/foreign ACKs are ignored" had no test — deleting the SEQ match (or the `len == 2` check) from the ACK guard kept all 20 tests green | Important | **Fixed**: test 22 covers stale SEQ, future SEQ, error-ACK with wrong SEQ and wrong ACK length; mutation-checked to be non-vacuous |
| Spec claimed heartbeats only "when the link is idle" while the code also emits them while a frame awaits ACK | Minor | **Fixed**: spec §6 wording and §7.1 diagram now show the `WAIT_ACK` self-loop |
| `cp_session_t` comment claimed "~600 B"; real size is 1100 B | Minor | **Fixed**: measured sizes documented in `coproc_session.h` / `coproc_proto.h` |
| `CP_FRAME_MAX` unreferenced by docs; caller buffer requirements unstated | Minor | **Fixed**: `send`/`tick`/`feed` header comments state the required buffer sizes |
| `cp_session_send(cmd = CP_CMD_ACK)` is unguarded (a caller could pend a reply frame and inevitably FAULT) | Minor / declined to judge | **Ruled**: no runtime guard — the API contract is documented instead ("callers must not send `CP_CMD_ACK`"), keeping the library free of checks the frozen design does not define |
| SEQ wrap interacting with de-duplication had no dedicated test | Minor / declined to judge | **Fixed anyway**: test 23 (6 lines, closes the gap) |
| Out-of-order older `SEQ` would be delivered again | Declined to judge | **Ruled**: matches the frozen design ("a new SEQ → deliver"); stop-and-wait means a peer cannot reorder, so no change |
| Heartbeat firing during an ACK wait window (behaviour itself) | Declined to judge | **Ruled**: conforms to the frozen design; only the documentation was corrected |

## 5. Deviations from the plan, and the ruling log

Four deviations were taken from the written plan:

1. Isolation workspace = the existing `m2-coproc` branch instead of a new worktree: the
   project's serial-capture and CubeProgrammer paths are pinned to this checkout.
2. Task 1 is a RED-scaffold task, so its gate is "the new tests fail as specified" plus a
   green firmware build rather than a green suite.
3. MISRA add-on wired as a **scoped CI step** (`stm32/middleware` only) rather than added to
   the existing all-directory step: the add-on reports 44 `misra-config` false positives in
   M1 legacy code (FatFs enumerators it cannot resolve), which is out of M2.1 scope. Extending
   it repo-wide needs the add-on's rule-text configuration first (tracked in `AGENTS.md` §10).
4. Three tests (21–23) added beyond the frozen 20-case list as review regression coverage; the
   original numbering is unchanged.

Every decision taken while executing the plan is reproduced below, so this report is the
complete record. The git-ignored SDD workspace that held the working ledger
(`.superpowers/sdd/2026-11-20-m2-1-coproc-proto-tdd/`) was deleted once the review closed.

| # | Ruling | Why | Cost if wrong |
|---|---|---|---|
| 1 | Work on the existing `m2-coproc` branch rather than a new worktree | serial-capture (`C:\temp`) and CubeProgrammer paths are pinned to this checkout, and it is an NTFS drive | none — branch containment already isolates the work |
| 2 | Plan Task 1 Step 1 (create the branch) skipped | the branch already existed from the documentation phase | none |
| 3 | Task 1's completion gate = "the 2 CRC tests fail exactly as specified" + green firmware build, not a green suite | Task 1 is the RED scaffold; a green suite is impossible by design | re-gated immediately by Task 2's green run |
| 4 | MISRA add-on wired as a CI step scoped to `stm32/middleware` | the add-on reports 44 `misra-config` false positives on M1 legacy code, outside M2.1 scope | middleware-only coverage until the add-on is configured for the whole tree |
| 5 | Commit subject extended beyond the brief's wording | the commit also carries the `ci.yml` change | none |
| 6 | Extra `AGENTS.md` consistency edits beyond the brief's three | a stale §3 date and branch line, §7 push refspec, §2 index rows and §10 MISRA status would mislead the next session | none |
| 7 | Review finding I-1 accepted as a real defect (not a nit) | `uint32_t` wrap makes `now >= deadline` time out early; fixed with a signed-difference comparison, pinned by test 21 (RED observed at `test_coproc_session.c:338`, then GREEN) | none |
| 8 | Review finding I-2 accepted as a test gap | test 22 added and proven non-vacuous by two mutations (dropping the SEQ match, dropping the `len == 2` guard) — each made it fail | none |
| 9 | Review D-1: no runtime guard on `cp_session_send(cmd = CP_CMD_ACK)` | the frozen design defines no such check, so the library stays minimal; the contract is documented in the header instead | a caller could pend a reply frame and reach FAULT after 4 timeouts |
| 10 | Review D-3 and D-4: behaviour kept as-is | both match the frozen design — stop-and-wait cannot reorder frames, and heartbeat timing is as specified | a future multi-outstanding transport would need explicit re-delivery rules |
| 11 | Review D-2: SEQ wrap tested anyway (test 23) | six lines that close an untested interaction between counter wrap and de-duplication | none |

## 6. What this unlocks for M2.2

- `CommTask` on the STM32 side calls only `cp_session_send` / `cp_session_tick` /
  `cp_session_feed`; the UART RX path needs USART1 IDLE + DMA (`.ioc` still lacks it — to be
  added in CubeMX), and feeds `cp_session_feed` in a `while (consumed < len)` loop, which the
  parser contract supports for arbitrary chunk sizes.
- The ESP32-S3 firmware compiles the same two `.c` files unmodified, so link behaviour is
  shared by construction; the two known asymmetric traps (rollover comparison, heartbeat
  wording) are now written down in the spec.
- `TIME_SET` (0x50, ESP32 → STM32, 4-byte big-endian Unix time) is the agreed hook for the
  RTC work in the board-wait task pool.

## 7. Not yet claimed

No board evidence exists for this library yet — the ESP32-S3 has not arrived. M2.1 claims cover
host-verifiable behaviour only. Real-link evidence (UART timing, chunk boundaries, error
injection on wire, end-to-end MQTT) belongs to M2.2/M2.3 and will get its own report.

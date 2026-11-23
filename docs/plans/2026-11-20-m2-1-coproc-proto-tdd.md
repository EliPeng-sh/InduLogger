# M2.1 Coprocessor Protocol Library — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `stm32/middleware/coproc_proto/` — a HAL-free, RTOS-free, allocation-free C library (frame codec + stop-and-wait session) for the STM32↔ESP32-S3 UART link — fully verified on host via Unity TDD (20 tests), wired into firmware build and CI, plus the English protocol specification deliverable.

**Architecture:** Two-layer pure C library. Frame layer (`coproc_proto.{h,c}`): CRC-16/CCITT-False, byte-frame encoder, streaming parser with SOF resync. Session layer (`coproc_session.{h,c}`): stop-and-wait ACK/retransmit/fault, dedup, heartbeat probing, link supervision, statistics. All timing is injected (`now_ms` parameters) so every state transition is host-testable. In M2.2 the CommTask wraps this library; the ESP32 side compiles the same sources.

**Tech Stack:** C11, Unity v2.6.0 (vendored at `host/third_party/unity/`), host build CMake+Ninja, firmware build make+arm-none-eabi-gcc, CI = GitHub Actions (host-tests / firmware-build / cppcheck).

**Spec:** `docs/specs/indulogger-design.md` §7 (protocol), §8 (layering: `middleware/coproc_proto/`), §14 (M2.1 first step). Frozen design decisions (user-approved 2026-11-20) are restated below — this plan is self-contained.

## Frozen Design (approved 2026-11-20 — do not redesign, only implement)

**Frame format** (all multi-byte fields big-endian):

```
Offset: 0     1     2     3     4    5    6..(5+N) (6+N) (7+N)
        SOF0  SOF1  LEN_H LEN_L SEQ  CMD  PAYLOAD   CRC_H CRC_L
        0xA5  0x5A
```

- `LEN` = payload byte count, 0..512. **Total frame length = 8 + N** (minimum frame = 8 bytes).
- CRC-16/CCITT-False: poly `0x1021`, init `0xFFFF`, no reflection, xorout `0`. `check("123456789") = 0x29B1`. Coverage: bytes from `LEN_H` through end of payload (offset 2 .. 5+N). Stored big-endian.
- Golden vector (verified with an independent Python oracle): empty HEARTBEAT frame, SEQ=0, CMD=0x02 → `A5 5A 00 00 00 02 A4 82`. (`CP_FRAME_MAX = 520` = 6 header + 512 payload + 2 CRC.)

**CMD codes** — `PING 0x01` / `HEARTBEAT 0x02` / `DATA_REPORT 0x10` / `DIAG_REPORT 0x11` (STM32→ESP) / `MQTT_PUB 0x20` / `MQTT_SUB 0x21` / `WIFI_CONFIG_SET 0x30` / `WIFI_CONFIG_GET 0x31` (STM32→ESP) / `NET_STATUS 0x40` (ESP→STM) / `TIME_SET 0x50` (**ESP32→STM32** — direction fixed 2026-11-20; spec §7 corrected) / `ACK 0x60`.

**ACK frame:** payload = `[SEQ(1B), STATUS(1B)]`; the ACK's own header SEQ field is always 0. ACK frames are never ACKed, never deduped, never delivered. STATUS: `0x00 OK`, `0x01 ERR_UNKNOWN_CMD`, `0x02 ERR_BAD_LEN`, `0x03 ERR_BAD_ARG`.

**Semantics (frozen):**
- CRC error → silent drop, no NACK (SEQ is untrustworthy), parser resyncs.
- Claimed LEN > 512 → drop immediately at LEN_L, parser resets.
- Unknown CMD with valid CRC → NACK `STATUS=0x01`, not delivered, `rx_nacks++`.
- ACK `STATUS=0` → confirm pending (clear, `tx_acked++`, `tx_seq++`). ACK `STATUS≠0` → immediate failure, **no retry** (`tx_rejects++`, `tx_seq++` still advances).
- ACK payload SEQ must match the pending frame's SEQ; stale/foreign ACKs are ignored.
- Retransmit: on deadline (500 ms) resend **same frame, same SEQ**, up to 3 retries; after the 3rd missed deadline → FAULT (drop frame, `tx_faults++`, `link=DOWN`). Sending while a frame is pending → `CP_ERR_BUSY`.
- HEARTBEAT: fire-and-forget (never pending, never ACKed), emitted by `tick` every 5 s; refreshes `last_peer_ms` on the receiver but never touches dedup state.
- Dedup (receiver): first CRC-valid data frame is always delivered (`seq_seen` flag); a repeat of `last_good_seq` → not delivered, `rx_dup++`, re-ACK(OK). A new SEQ → deliver, update `last_good_seq`, ACK(OK). Data-frame SEQs start at 0 and wrap.
- Link supervision: any CRC-valid frame refreshes `last_peer_ms`; 15 s without a frame → `tick` sets `link=DOWN`; any valid frame received while DOWN → `UP`. `DOWN` does not stop heartbeat probing (M2.2 recovery semantics).

**Tunables (M2.2 may adjust values; structure is frozen):** `CP_ACK_TIMEOUT_MS 500`, `CP_RETRY_MAX 3`, `CP_HEARTBEAT_PERIOD_MS 5000`, `CP_LINK_DOWN_MS 15000`, `CP_PAYLOAD_MAX 512`.

**Layering:** frame layer `coproc_proto.{h,c}` + session layer `coproc_session.{h,c}` in `stm32/middleware/coproc_proto/`. Pure C, zero HAL/RTOS includes, zero malloc, no global mutable state, all timing via injected `now_ms`. ESP32 side compiles the same files.

## Global Constraints

- Branch: create `m2-coproc` from current `m1-skeleton` HEAD before Task 1; one commit per task (Conventional Commits, scopes `m2.1`/docs); author `Eli Peng <xjmps7114@qq.com>`.
- **Never `git add -A` / `git add .`**: an untracked foreign file `FW_SU585_WP2_Singleboard_Injection_Report_20261105.md` sits in the repo root — always add explicit paths only.
- Pure C11 only: no HAL/RTOS/CMSIS headers, no `malloc`, no static/global mutable state in library files; `<string.h>` and `<stdint.h>/<stddef.h>` allowed.
- Hand-written code compiles under `-Wall -Wextra` (added via Makefile pattern rule in Task 1).
- Host tests: Unity v2.6.0 vendored at `host/third_party/unity/`; **each test file carries its own `main()`** (`UNITY_BEGIN` / `RUN_TEST` / `UNITY_END`).
- Commands (from `/mnt/d/InduLogger`):
  - Host build+test: `cmake -S host -B host/build -G Ninja && cmake --build host/build && ctest --test-dir host/build --output-on-failure`
  - Firmware build (must stay green): `TMPDIR=/home/eli_peng/tmp make -C /mnt/d/InduLogger/stm32 -j4`
  - Push (explicit refspec — tag/branch name collision makes bare pushes fail): `git push origin refs/heads/m2-coproc && git push gitee refs/heads/m2-coproc`
  - CI check: `curl -s https://api.github.com/repos/EliPeng-sh/InduLogger/actions/runs?per_page=1`
- Reply to the user in Chinese; code, commits, and document text in English.

## Review Focus

The five failure modes most likely to bite the M2.2 CommTask integrator, each pinned by a test in the task that owns the code:

1. **UART delivers bytes in arbitrary chunks** — the parser must buffer partial frames across calls and only report completion when the CRC byte lands. Pinned by Task 4 test 6 (byte-by-byte feed) and Task 6 test 18 (7-byte-chunk fuzz).
2. **Payload bytes may contain the SOF pair `A5 5A`** — false re-sync inside a payload would corrupt delivery. Pinned by Task 4 test 8.
3. **A corrupted frame must not wedge the link** — parser must resync after CRC failure and after an over-length LEN, and the session must keep accepting later good frames. Pinned by Task 4 tests 9–10 and Task 6 test 18.
4. **Stale or foreign ACKs must not confirm the wrong pending frame** — ACK matching requires payload SEQ == pending SEQ and pending_active. Pinned by Task 5 tests 12/15 (exact-SEQ ACK confirm; STATUS≠0 reject must not retransmit).
5. **Arbitrary garbage must not corrupt session state or crash** — 1 KB of pseudo-random bytes through `cp_session_feed` must yield only defined return codes, then normal operation must resume. Pinned by Task 6 test 18 (with a deterministic 520-byte zero drain before the recovery frames so any phantom frame is forced to complete and CRC-fail).

---

### Task 1: Scaffold middleware, build wiring, CRC tests (RED)

**Files:**
- Create: `stm32/middleware/coproc_proto/coproc_proto.h`
- Create: `stm32/middleware/coproc_proto/coproc_session.h`
- Create: `stm32/middleware/coproc_proto/coproc_proto.c` (stubs)
- Create: `stm32/middleware/coproc_proto/coproc_session.c` (stubs)
- Create: `host/tests/test_coproc_proto.c` (tests 1–2 only at this stage)
- Modify: `host/CMakeLists.txt` (line 9 source list, line 10 include list)
- Modify: `stm32/Makefile` (line 45 `OUR_SRC`, pattern-rules block ~line 63-66)
- Modify: `.github/workflows/ci.yml` (line 30 cppcheck dir loop)

**Interfaces:**
- Consumes: Unity at `host/third_party/unity/` (existing glob `tests/test_*.c` in `host/CMakeLists.txt`).
- Produces: full public API of both headers (exact signatures below — every later task implements against these, verbatim).

- [x] **Step 1: Create the branch**

```bash
cd /mnt/d/InduLogger && git checkout -b m2-coproc
```

- [x] **Step 2: Write `stm32/middleware/coproc_proto/coproc_proto.h`** (full file — final form, not a stub)

```c
#ifndef COPROC_PROTO_H
#define COPROC_PROTO_H

#include <stddef.h>
#include <stdint.h>

/* ---- Tunables (M2.2 may adjust values; structure is frozen) ---- */
#define CP_SOF0           0xA5u
#define CP_SOF1           0x5Au
#define CP_PAYLOAD_MAX    512u
#define CP_FRAME_MAX      520u  /* 2 SOF + 4 hdr + CP_PAYLOAD_MAX + 2 CRC */

/* ---- CMD codes (v1 set; OTA group reserved for v2) ---- */
#define CP_CMD_PING             0x01u
#define CP_CMD_HEARTBEAT        0x02u
#define CP_CMD_DATA_REPORT      0x10u
#define CP_CMD_DIAG_REPORT      0x11u
#define CP_CMD_MQTT_PUB         0x20u
#define CP_CMD_MQTT_SUB         0x21u
#define CP_CMD_WIFI_CONFIG_SET  0x30u
#define CP_CMD_WIFI_CONFIG_GET  0x31u
#define CP_CMD_NET_STATUS       0x40u
#define CP_CMD_TIME_SET         0x50u  /* ESP32 -> STM32 (direction fixed 2026-11-20) */
#define CP_CMD_ACK              0x60u

/* ---- ACK status codes (ACK payload = [SEQ(1B), STATUS(1B)]) ---- */
#define CP_ACK_OK               0x00u
#define CP_ACK_ERR_UNKNOWN_CMD  0x01u
#define CP_ACK_ERR_BAD_LEN      0x02u
#define CP_ACK_ERR_BAD_ARG      0x03u

typedef enum {
    CP_OK = 0,
    CP_ERR_ARG,
    CP_ERR_BUF,
    CP_ERR_LEN,
    CP_ERR_CRC,
    CP_ERR_NO_FRAME,
    CP_ERR_BUSY
} cp_status_t;

typedef struct {
    uint8_t  seq;
    uint8_t  cmd;
    uint16_t len;                        /* payload bytes, 0..CP_PAYLOAD_MAX */
    uint8_t  payload[CP_PAYLOAD_MAX];
} cp_frame_t;

/* ---- Streaming parser (survives arbitrary byte chunking) ---- */
typedef enum {
    CP_P_SOF0, CP_P_SOF1, CP_P_LEN_H, CP_P_LEN_L,
    CP_P_SEQ,  CP_P_CMD,  CP_P_PAYLOAD,
    CP_P_CRC_H, CP_P_CRC_L
} cp_parse_state_t;

typedef struct {
    cp_parse_state_t state;
    uint8_t  hdr[4];   /* LEN_H, LEN_L, SEQ, CMD; reused for CRC_H at CRC stage */
    uint16_t len;      /* claimed payload length */
    uint16_t got;      /* payload bytes received so far */
    uint8_t  payload[CP_PAYLOAD_MAX];
} cp_parser_t;         /* ~530 B, allocate statically */

/* CRC-16/CCITT-False (poly 0x1021, init 0xFFFF, no reflect, xorout 0). */
uint16_t cp_crc16(const uint8_t *data, size_t len);

/* Encode one frame; returns total bytes (8+len), 0 on error
 * (NULL args, f->len > CP_PAYLOAD_MAX, buf_sz too small). */
size_t cp_encode(const cp_frame_t *f, uint8_t *buf, size_t buf_sz);

void cp_parser_init(cp_parser_t *p);

/* Feed len bytes; returns:
 * CP_OK         - complete frame in *out, *consumed = bytes through CRC_L
 * CP_ERR_NO_FRAME - bytes buffered, frame incomplete, *consumed = len
 * CP_ERR_CRC    - frame dropped, parser reset, *consumed = through CRC_L
 * CP_ERR_LEN    - over-length frame dropped, parser reset,
 *                 *consumed = through LEN_L
 * CP_ERR_ARG    - NULL p/data/consumed
 * out may be NULL (validate-only). Caller re-feeds from data+*consumed. */
cp_status_t cp_parse(cp_parser_t *p, const uint8_t *data, size_t len,
                     cp_frame_t *out, size_t *consumed);

#endif /* COPROC_PROTO_H */
```

- [x] **Step 3: Write `stm32/middleware/coproc_proto/coproc_session.h`** (full file — final form)

```c
#ifndef COPROC_SESSION_H
#define COPROC_SESSION_H

#include <stddef.h>
#include <stdint.h>
#include "coproc_proto.h"

/* ---- Tunables (M2.2 may adjust values; structure is frozen) ---- */
#define CP_ACK_TIMEOUT_MS       500u
#define CP_RETRY_MAX            3u
#define CP_HEARTBEAT_PERIOD_MS  5000u
#define CP_LINK_DOWN_MS         15000u

typedef enum { CP_LINK_UP = 0, CP_LINK_DOWN } cp_link_state_t;

typedef struct {
    cp_parser_t     parser;        /* buffered bytes across feed calls */
    /* sender (stop-and-wait) */
    uint8_t         tx_seq;
    uint8_t         pending_active;
    cp_frame_t      pending;
    uint32_t        deadline_ms;
    uint8_t         retries;
    /* receiver (dedup) */
    uint8_t         last_good_seq;
    uint8_t         seq_seen;
    /* link supervision */
    uint32_t        last_peer_ms;
    uint32_t        last_hb_ms;
    cp_link_state_t link;
    /* statistics */
    uint32_t tx_frames, tx_acked, tx_rejects, tx_retries, tx_faults;
    uint32_t rx_frames, rx_dup, rx_crc_err, rx_nacks;
} cp_session_t;                    /* ~600 B, allocate statically */

void cp_session_init(cp_session_t *s, uint32_t now_ms);

/* Queue one command frame (stop-and-wait). Emits the encoded frame at once.
 * HEARTBEAT goes fire-and-forget (never pends). CP_ERR_BUSY if a frame is
 * already pending. out_len = bytes written to buf. */
cp_status_t cp_session_send(cp_session_t *s, uint8_t cmd, const uint8_t *pl,
                            size_t len, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len);

/* Periodic driver. At most one emitted frame per call; retransmit has
 * priority over heartbeat. Also performs link-down supervision.
 * Returns CP_ERR_NO_FRAME when nothing was emitted. */
cp_status_t cp_session_tick(cp_session_t *s, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len);

/* Feed received bytes. Processes at most one frame per call.
 * out->cmd == 0 means "nothing delivered" (frame consumed internally or
 * none completed). ack_buf/ack_len receive the encoded ACK/NACK frame
 * (0 = no ACK emitted). consumed = bytes consumed from data. */
cp_status_t cp_session_feed(cp_session_t *s, const uint8_t *data, size_t len,
                            uint32_t now_ms, cp_frame_t *out, size_t *consumed,
                            uint8_t *ack_buf, size_t ack_buf_sz, size_t *ack_len);

#endif /* COPROC_SESSION_H */
```

- [x] **Step 4: Write RED stubs `coproc_proto.c`**

```c
#include "coproc_proto.h"

/* RED stubs: symbols exist so the suite links; behaviour tests fail. */

uint16_t cp_crc16(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return 0x0000u;
}

size_t cp_encode(const cp_frame_t *f, uint8_t *buf, size_t buf_sz)
{
    (void)f;
    (void)buf;
    (void)buf_sz;
    return 0u;
}

void cp_parser_init(cp_parser_t *p)
{
    (void)p;
}

cp_status_t cp_parse(cp_parser_t *p, const uint8_t *data, size_t len,
                     cp_frame_t *out, size_t *consumed)
{
    (void)p;
    (void)data;
    (void)len;
    (void)out;
    if (consumed != NULL) {
        *consumed = 0u;
    }
    return CP_ERR_ARG;
}
```

- [x] **Step 5: Write RED stubs `coproc_session.c`**

```c
#include "coproc_session.h"

/* RED stubs: symbols exist so the suite links; behaviour tests fail. */

void cp_session_init(cp_session_t *s, uint32_t now_ms)
{
    (void)s;
    (void)now_ms;
}

cp_status_t cp_session_send(cp_session_t *s, uint8_t cmd, const uint8_t *pl,
                            size_t len, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    (void)s; (void)cmd; (void)pl; (void)len; (void)now_ms; (void)buf; (void)buf_sz;
    if (out_len != NULL) {
        *out_len = 0u;
    }
    return CP_ERR_ARG;
}

cp_status_t cp_session_tick(cp_session_t *s, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    (void)s; (void)now_ms; (void)buf; (void)buf_sz;
    if (out_len != NULL) {
        *out_len = 0u;
    }
    return CP_ERR_ARG;
}

cp_status_t cp_session_feed(cp_session_t *s, const uint8_t *data, size_t len,
                            uint32_t now_ms, cp_frame_t *out, size_t *consumed,
                            uint8_t *ack_buf, size_t ack_buf_sz, size_t *ack_len)
{
    (void)s; (void)data; (void)len; (void)now_ms; (void)out; (void)ack_buf;
    (void)ack_buf_sz;
    if (consumed != NULL) {
        *consumed = 0u;
    }
    if (ack_len != NULL) {
        *ack_len = 0u;
    }
    return CP_ERR_ARG;
}
```

- [x] **Step 6: Write `host/tests/test_coproc_proto.c`** with CRC tests only (tests 3-11 are appended in Tasks 3-4)

```c
#include <string.h>
#include "unity.h"
#include "coproc_proto.h"

void setUp(void)    { }
void tearDown(void) { }

/* Golden vector verified with an independent Python oracle:
 * CRC-16/CCITT-False over {00 00 00 02} = 0xA482. */
static const uint8_t GOLDEN_HB[8] =
    { 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x02, 0xA4, 0x82 };

/* Test 1: CRC-16/CCITT-False check vector. */
static void test_crc_check_vector(void)
{
    uint16_t crc = cp_crc16((const uint8_t *)"123456789", 9u);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc);
}

/* Test 2: empty input keeps the init value 0xFFFF. */
static void test_crc_empty_input(void)
{
    uint8_t b = 0x00;
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, cp_crc16(&b, 0u));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, cp_crc16(NULL, 0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_check_vector);
    RUN_TEST(test_crc_empty_input);
    return UNITY_END();
}
```

- [x] **Step 7: Wire the build.** In `host/CMakeLists.txt`: the `add_executable(...)` source list ends with `${UNITY_DIR}/unity.c)` — insert ` ../stm32/middleware/coproc_proto/coproc_proto.c ../stm32/middleware/coproc_proto/coproc_session.c` immediately before `${UNITY_DIR}/unity.c)`; the include list ends with `../stm32 ${UNITY_DIR}` — append ` ../stm32/middleware/coproc_proto`. In `stm32/Makefile`: line 45 becomes

```make
OUR_SRC := $(wildcard app/*/*.c platform/*/*.c bsp/*/*.c middleware/*/*.c)
```

and next to the existing per-dir pattern rules (around lines 63-66) add

```make
$(BUILD)/middleware/%.o: CFLAGS_EXTRA := -Wextra
```

In `.github/workflows/ci.yml` line 30 the cppcheck loop becomes

```yaml
          for d in stm32/app stm32/bsp stm32/platform stm32/middleware host; do
```

- [x] **Step 8: Run host tests — verify RED**

Run: `cmake -S host -B host/build -G Ninja && cmake --build host/build && ctest --test-dir host/build -R coproc_proto --output-on-failure`
Expected: builds and links (stubs provide all symbols); both CRC tests FAIL (stub returns 0x0000).

- [x] **Step 9: Verify firmware still builds**

Run: `TMPDIR=/home/eli_peng/tmp make -C /mnt/d/InduLogger/stm32 -j4`
Expected: builds clean with `-Wall -Wextra` on the new middleware objects.

- [x] **Step 10: Commit**

```bash
git add stm32/middleware/coproc_proto/coproc_proto.h stm32/middleware/coproc_proto/coproc_session.h stm32/middleware/coproc_proto/coproc_proto.c stm32/middleware/coproc_proto/coproc_session.c host/tests/test_coproc_proto.c host/CMakeLists.txt stm32/Makefile .github/workflows/ci.yml
git commit -m "feat(m2.1): scaffold coproc_proto middleware, build wiring, CRC tests (RED)"
```

---

### Task 2: Implement CRC-16/CCITT-False (GREEN)

**Files:**
- Modify: `stm32/middleware/coproc_proto/coproc_proto.c` (replace `cp_crc16` stub)
- Modify: `stm32/middleware/coproc_proto/coproc_session.c` (unchanged this task)

**Interfaces:**
- Consumes: `cp_crc16` signature from Task 1 header.
- Produces: `cp_crc16` real behaviour — used by `cp_encode`, `cp_parse`, and a chaining helper `crc_update` (also reused by the parser in Task 4).

- [x] **Step 1: Tests already exist and fail** (Task 1 Step 8).

- [x] **Step 2: Implement `cp_crc16`** — replace the stub in `coproc_proto.c` with:

```c
#include "coproc_proto.h"

/* CRC-16/CCITT-False: poly 0x1021, init 0xFFFF, no reflection, xorout 0.
 * check("123456789") == 0x29B1 (independent Python oracle verified).
 * crc_update chains a running CRC so callers can cover a frame assembled
 * from disjoint buffers (parser reuses it for header + payload). */
static uint16_t crc_update(uint16_t crc, const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t cp_crc16(const uint8_t *data, size_t len)
{
    return crc_update(0xFFFFu, data, len);
}
```

- [x] **Step 3: Run host tests — verify GREEN (CRC subset)**

Run: `cmake --build host/build && ctest --test-dir host/build -R coproc_proto --output-on-failure`
Expected: `test_crc_check_vector`, `test_crc_empty_input` PASS.

- [x] **Step 4: Commit**

```bash
git add stm32/middleware/coproc_proto/coproc_proto.c
git commit -m "feat(m2.1): implement CRC-16/CCITT-False with chaining helper (GREEN)"
```

---

### Task 3: Frame encoder + golden vector tests

**Files:**
- Modify: `stm32/middleware/coproc_proto/coproc_proto.c` (replace `cp_encode` stub)
- Modify: `host/tests/test_coproc_proto.c` (append tests 3-4 and register in `main`)

**Interfaces:**
- Consumes: `cp_crc16` (Task 2), `cp_frame_t` (Task 1).
- Produces: `cp_encode` — used by every session test (Task 5/6 ACK construction) and by CommTask (M2.2).

- [x] **Step 1: Append failing tests to `host/tests/test_coproc_proto.c`** (before `main`)

```c
/* Test 3: encode the golden heartbeat frame byte-for-byte. */
static void test_encode_golden_heartbeat(void)
{
    cp_frame_t f;
    uint8_t buf[16];
    memset(&f, 0, sizeof(f));
    f.seq = 0u;
    f.cmd = CP_CMD_HEARTBEAT;
    f.len = 0u;
    size_t n = cp_encode(&f, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(8u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(GOLDEN_HB, buf, 8);
}

/* Test 4: payload framing, CRC placement, buffer-too-small. */
static void test_encode_payload_layout(void)
{
    cp_frame_t f;
    uint8_t buf[16];
    memset(&f, 0, sizeof(f));
    f.seq = 0x07u;
    f.cmd = CP_CMD_DATA_REPORT;
    f.len = 3u;
    f.payload[0] = 0x01u;
    f.payload[1] = 0x02u;
    f.payload[2] = 0x03u;
    size_t n = cp_encode(&f, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(11u, n);
    TEST_ASSERT_EQUAL_HEX8(0xA5, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_DATA_REPORT, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[7]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[8]);
    uint16_t crc = cp_crc16(&buf[2], 7u);        /* LEN_H .. payload end */
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), buf[9]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc & 0xFFu), buf[10]);
    TEST_ASSERT_EQUAL_UINT(0u, cp_encode(&f, buf, 10u));  /* 11 needed */
    TEST_ASSERT_EQUAL_UINT(0u, cp_encode(NULL, buf, sizeof(buf)));
}
```

Register in `main` before `return UNITY_END();`:

```c
    RUN_TEST(test_encode_golden_heartbeat);
    RUN_TEST(test_encode_payload_layout);
```

- [x] **Step 2: Run — verify RED**

Run: `cmake --build host/build && ctest --test-dir host/build -R coproc_proto --output-on-failure`
Expected: the two new tests FAIL (stub returns 0).

- [x] **Step 3: Implement `cp_encode`** — replace the stub in `coproc_proto.c`:

```c
size_t cp_encode(const cp_frame_t *f, uint8_t *buf, size_t buf_sz)
{
    if (f == NULL || buf == NULL || f->len > CP_PAYLOAD_MAX) {
        return 0u;
    }
    const size_t total = 6u + (size_t)f->len + 2u;
    if (buf_sz < total) {
        return 0u;
    }
    buf[0] = CP_SOF0;
    buf[1] = CP_SOF1;
    buf[2] = (uint8_t)(f->len >> 8);
    buf[3] = (uint8_t)(f->len & 0xFFu);
    buf[4] = f->seq;
    buf[5] = f->cmd;
    for (uint16_t i = 0; i < f->len; i++) {
        buf[6u + i] = f->payload[i];
    }
    uint16_t crc = cp_crc16(&buf[2], 4u + (size_t)f->len);
    buf[6u + f->len] = (uint8_t)(crc >> 8);
    buf[7u + f->len] = (uint8_t)(crc & 0xFFu);
    return total;
}
```

- [x] **Step 4: Run — verify GREEN**

Run: `cmake --build host/build && ctest --test-dir host/build -R coproc_proto --output-on-failure`
Expected: tests 1-4 all PASS.

- [x] **Step 5: Commit**

```bash
git add stm32/middleware/coproc_proto/coproc_proto.c host/tests/test_coproc_proto.c
git commit -m "feat(m2.1): frame encoder with golden-vector tests"
```

---

### Task 4: Streaming parser with SOF resync

**Files:**
- Modify: `stm32/middleware/coproc_proto/coproc_proto.c` (implement `cp_parser_init`, `cp_parse`)
- Modify: `host/tests/test_coproc_proto.c` (append tests 5-11 and register)

**Interfaces:**
- Consumes: `crc_update` (Task 2), `cp_encode` (Task 3, used to build test frames).
- Produces: `cp_parse` contract (buffering, consumed semantics, CP_ERR_CRC / CP_ERR_LEN resets) — consumed by `cp_session_feed` in Tasks 5-6 and by CommTask RX path in M2.2.

- [x] **Step 1: Append failing tests to `host/tests/test_coproc_proto.c`**

```c
/* Test 5: golden frame in one shot. */
static void test_parse_golden_one_shot(void)
{
    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, GOLDEN_HB, sizeof(GOLDEN_HB), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(8u, consumed);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.seq);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
    TEST_ASSERT_EQUAL_UINT16(0u, out.len);
}

/* Test 6: byte-by-byte feed must buffer across calls. */
static void test_parse_byte_by_byte(void)
{
    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    for (size_t i = 0; i < 7u; i++) {
        cp_status_t st = cp_parse(&p, &GOLDEN_HB[i], 1u, &out, &consumed);
        TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME, st);
        TEST_ASSERT_EQUAL_UINT(1u, consumed);
    }
    cp_status_t st = cp_parse(&p, &GOLDEN_HB[7], 1u, &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(1u, consumed);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
}

/* Test 7: noise wrapping the frame (contains an unpaired 0xA5). */
static void test_parse_noise_wrapped(void)
{
    static const uint8_t noise_and_frame[12] = {
        0x11, 0x22, 0xA5, 0x33,                     /* noise, unpaired SOF0 */
        0xA5, 0x5A, 0x00, 0x00, 0x00, 0x02, 0xA4, 0x82
    };
    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, noise_and_frame, sizeof(noise_and_frame),
                              &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(12u, consumed);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
}

/* Test 8: payload containing the SOF pair must not re-sync. */
static void test_parse_payload_contains_sof(void)
{
    cp_frame_t f;
    uint8_t buf[16];
    memset(&f, 0, sizeof(f));
    f.seq = 1u;
    f.cmd = CP_CMD_DATA_REPORT;
    f.len = 4u;
    f.payload[0] = 0xA5u;
    f.payload[1] = 0x5Au;
    f.payload[2] = 0x7Eu;
    f.payload[3] = 0x00u;
    size_t n = cp_encode(&f, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(12u, n);

    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, buf, n, &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(n, consumed);
    TEST_ASSERT_EQUAL_UINT16(4u, out.len);
    TEST_ASSERT_EQUAL_HEX8(0xA5, out.payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, out.payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x7E, out.payload[2]);
}

/* Test 9: corrupted CRC -> dropped, parser reusable immediately. */
static void test_parse_crc_corrupt_drops(void)
{
    uint8_t bad[8];
    memcpy(bad, GOLDEN_HB, sizeof(bad));
    bad[7] ^= 0xFFu;

    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, bad, sizeof(bad), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_ERR_CRC, st);
    TEST_ASSERT_EQUAL_UINT(8u, consumed);

    st = cp_parse(&p, GOLDEN_HB, sizeof(GOLDEN_HB), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
}

/* Test 10: over-length LEN -> CP_ERR_LEN consumed through LEN_L (4 bytes),
 * parser reset; re-feed skips the garbage, next frame parses. */
static void test_parse_oversize_len(void)
{
    static const uint8_t data[18] = {
        0xA5, 0x5A, 0xFF, 0xFF, 0x00, 0x02,        /* LEN=0xFFFF > 512 */
        0x11, 0x22, 0x33, 0x44,                    /* garbage tail     */
        0xA5, 0x5A, 0x00, 0x00, 0x00, 0x02, 0xA4, 0x82
    };
    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, data, sizeof(data), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_ERR_LEN, st);
    TEST_ASSERT_EQUAL_UINT(4u, consumed);          /* SOF0,SOF1,LEN_H,LEN_L */

    st = cp_parse(&p, data + consumed, sizeof(data) - consumed, &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(14u, consumed);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
}

/* Test 11: two frames back-to-back -> two deliveries. */
static void test_parse_two_frames_back_to_back(void)
{
    uint8_t stream[8 + 12];
    memcpy(stream, GOLDEN_HB, 8u);

    cp_frame_t f2;
    memset(&f2, 0, sizeof(f2));
    f2.seq = 1u;
    f2.cmd = CP_CMD_DATA_REPORT;
    f2.len = 4u;
    f2.payload[0] = 'a';
    f2.payload[1] = 'b';
    f2.payload[2] = 'c';
    f2.payload[3] = 'd';
    size_t n2 = cp_encode(&f2, stream + 8, sizeof(stream) - 8u);
    TEST_ASSERT_EQUAL_UINT(12u, n2);

    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, stream, sizeof(stream), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(8u, consumed);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);

    st = cp_parse(&p, stream + consumed, sizeof(stream) - consumed,
                  &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(12u, consumed);
    TEST_ASSERT_EQUAL_HEX8('a', out.payload[0]);
}
```

Register in `main`:

```c
    RUN_TEST(test_parse_golden_one_shot);
    RUN_TEST(test_parse_byte_by_byte);
    RUN_TEST(test_parse_noise_wrapped);
    RUN_TEST(test_parse_payload_contains_sof);
    RUN_TEST(test_parse_crc_corrupt_drops);
    RUN_TEST(test_parse_oversize_len);
    RUN_TEST(test_parse_two_frames_back_to_back);
```

- [x] **Step 2: Run — verify RED**

Run: `cmake --build host/build && ctest --test-dir host/build -R coproc_proto --output-on-failure`
Expected: the seven new tests FAIL (stubs return `CP_ERR_ARG`).

- [x] **Step 3: Implement parser** — replace `cp_parser_init` and `cp_parse` stubs in `coproc_proto.c`:

```c
void cp_parser_init(cp_parser_t *p)
{
    if (p == NULL) {
        return;
    }
    p->state = CP_P_SOF0;
    p->hdr[0] = 0u;
    p->hdr[1] = 0u;
    p->hdr[2] = 0u;
    p->hdr[3] = 0u;
    p->len = 0u;
    p->got = 0u;
}

cp_status_t cp_parse(cp_parser_t *p, const uint8_t *data, size_t len,
                     cp_frame_t *out, size_t *consumed)
{
    if (p == NULL || data == NULL || consumed == NULL) {
        return CP_ERR_ARG;
    }
    *consumed = 0u;

    for (size_t i = 0; i < len; i++) {
        const uint8_t b = data[i];
        (*consumed)++;

        switch (p->state) {
        case CP_P_SOF0:
            if (b == CP_SOF0) {
                p->state = CP_P_SOF1;
            }
            break;

        case CP_P_SOF1:
            if (b == CP_SOF1) {
                p->state = CP_P_LEN_H;
            } else if (b == CP_SOF0) {
                p->state = CP_P_SOF1;      /* stay hunting */
            } else {
                p->state = CP_P_SOF0;
            }
            break;

        case CP_P_LEN_H:
            p->hdr[0] = b;
            p->state = CP_P_LEN_L;
            break;

        case CP_P_LEN_L:
            p->hdr[1] = b;
            p->len = (uint16_t)(((uint16_t)p->hdr[0] << 8) | p->hdr[1]);
            if (p->len > CP_PAYLOAD_MAX) {
                cp_parser_init(p);         /* drop, resync at LEN boundary */
                return CP_ERR_LEN;
            }
            p->state = CP_P_SEQ;
            break;

        case CP_P_SEQ:
            p->hdr[2] = b;
            p->state = CP_P_CMD;
            break;

        case CP_P_CMD:
            p->hdr[3] = b;
            p->got = 0u;
            p->state = (p->len == 0u) ? CP_P_CRC_H : CP_P_PAYLOAD;
            break;

        case CP_P_PAYLOAD:
            p->payload[p->got] = b;
            p->got++;
            if (p->got == p->len) {
                p->state = CP_P_CRC_H;
            }
            break;

        case CP_P_CRC_H:
            p->hdr[0] = b;                 /* LEN_H no longer needed */
            p->state = CP_P_CRC_L;
            break;

        case CP_P_CRC_L: {
            const uint16_t rx = (uint16_t)(((uint16_t)p->hdr[0] << 8) | b);
            const uint8_t  seq = p->hdr[2];
            const uint8_t  cmd = p->hdr[3];
            const uint16_t plen = p->len;
            uint8_t hdr4[4] = {
                (uint8_t)(plen >> 8), (uint8_t)(plen & 0xFFu), seq, cmd
            };
            uint16_t calc = crc_update(crc_update(0xFFFFu, hdr4, 4u),
                                       p->payload, plen);
            cp_parser_init(p);
            if (rx != calc) {
                return CP_ERR_CRC;         /* whole frame consumed */
            }
            if (out != NULL) {
                out->seq = seq;
                out->cmd = cmd;
                out->len = plen;
                for (uint16_t k = 0; k < plen; k++) {
                    out->payload[k] = p->payload[k];
                }
            }
            return CP_OK;
        }

        default:
            cp_parser_init(p);
            break;
        }
    }
    return CP_ERR_NO_FRAME;                /* buffered, caller re-feeds rest */
}
```

Note: `crc_update` is `static` in this file (Task 2) — the parser calls it directly.

- [x] **Step 4: Run — verify GREEN**

Run: `cmake --build host/build && ctest --test-dir host/build -R coproc_proto --output-on-failure`
Expected: tests 1-11 all PASS.

- [x] **Step 5: Commit**

```bash
git add stm32/middleware/coproc_proto/coproc_proto.c host/tests/test_coproc_proto.c
git commit -m "feat(m2.1): streaming frame parser with SOF resync"
```

---

### Task 5: Session core — send / ACK / retransmit / reject / FAULT

**Files:**
- Create: `host/tests/test_coproc_session.c` (tests 12-15)
- Modify: `stm32/middleware/coproc_proto/coproc_session.c` (implement `cp_session_init`, `cp_session_send`, `cp_session_tick`, `cp_session_feed` — full final form including dedup/NACK/heartbeat helpers; the behaviour tests for those land in Task 6)

**Interfaces:**
- Consumes: `cp_encode`, `cp_parse`, `cp_frame_t`, `CP_CMD_*`, `CP_ACK_*` (Tasks 1-4).
- Produces: full session API (final form); tests 12-15 pin the sender state machine.

- [x] **Step 1: Write `host/tests/test_coproc_session.c`** with tests 12-15 (tests 16-20 appended in Task 6)

```c
#include <string.h>
#include "unity.h"
#include "coproc_session.h"

void setUp(void)    { }
void tearDown(void) { }

static const uint8_t GOLDEN_HB[8] =
    { 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x02, 0xA4, 0x82 };

/* Build an ACK frame for feeding: [acked_seq, status]. */
static size_t make_ack(uint8_t *buf, size_t buf_sz, uint8_t acked_seq,
                       uint8_t status)
{
    cp_frame_t f;
    memset(&f, 0, sizeof(f));
    f.seq = 0u;                            /* ACK header SEQ is always 0 */
    f.cmd = CP_CMD_ACK;
    f.len = 2u;
    f.payload[0] = acked_seq;
    f.payload[1] = status;
    return cp_encode(&f, buf, buf_sz);
}

/* Test 12: send -> ACK(OK) confirms; pending cleared, seq advances. */
static void test_send_ack_ok_confirms(void)
{
    cp_session_t s;
    uint8_t txbuf[32];
    size_t n = 0u;
    cp_session_init(&s, 0u);

    cp_status_t st = cp_session_send(&s, CP_CMD_DATA_REPORT,
                                     (const uint8_t *)"xy", 2u, 1000u,
                                     txbuf, sizeof(txbuf), &n);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(10u, n);        /* 8 + 2 payload */
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_frames);
    TEST_ASSERT_EQUAL_UINT8(1u, s.pending_active);
    TEST_ASSERT_EQUAL_HEX8(0x00, txbuf[4]);       /* SEQ 0 */
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_DATA_REPORT, txbuf[5]);

    /* Sending again while pending must fail busy. */
    uint8_t b2[32];
    size_t n2 = 0u;
    TEST_ASSERT_EQUAL(CP_ERR_BUSY,
                      cp_session_send(&s, CP_CMD_DATA_REPORT,
                                      (const uint8_t *)"z", 1u, 1050u,
                                      b2, sizeof(b2), &n2));

    uint8_t ack[16];
    size_t alen = make_ack(ack, sizeof(ack), 0x00, CP_ACK_OK);
    cp_frame_t out;
    size_t consumed = 0u;
    size_t ack_out = 0u;
    st = cp_session_feed(&s, ack, alen, 1100u, &out, &consumed,
                         NULL, 0u, &ack_out);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(0u, out.cmd);   /* ACK consumed internally */
    TEST_ASSERT_EQUAL_UINT8(0u, s.pending_active);
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_acked);
    TEST_ASSERT_EQUAL_HEX8(0x01, s.tx_seq);
}

/* Test 13: deadline -> retransmit same frame, same SEQ. */
static void test_send_retransmit_on_deadline(void)
{
    cp_session_t s;
    uint8_t txbuf[32];
    size_t n = 0u;
    cp_session_init(&s, 0u);
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_send(&s, CP_CMD_DATA_REPORT,
                                      (const uint8_t *)"ab", 2u, 1000u,
                                      txbuf, sizeof(txbuf), &n));

    uint8_t tbuf[32];
    size_t tn = 0u;
    TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME,
                      cp_session_tick(&s, 1499u, tbuf, sizeof(tbuf), &tn));

    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 1500u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT(10u, tn);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(txbuf, tbuf, 10u);   /* same bytes, same SEQ */
    TEST_ASSERT_EQUAL_UINT8(1u, s.retries);
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_retries);
}

/* Test 14: three missed deadlines -> FAULT, link down. */
static void test_send_fault_after_retry_max(void)
{
    cp_session_t s;
    uint8_t txbuf[32];
    size_t n = 0u;
    cp_session_init(&s, 0u);
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_send(&s, CP_CMD_DATA_REPORT,
                                      (const uint8_t *)"ab", 2u, 1000u,
                                      txbuf, sizeof(txbuf), &n));

    uint8_t tbuf[32];
    size_t tn = 0u;
    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 1500u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 2000u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 2500u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT8(3u, s.retries);

    TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME,
                      cp_session_tick(&s, 3000u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT8(0u, s.pending_active);
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_faults);
    TEST_ASSERT_EQUAL(CP_LINK_DOWN, s.link);
}

/* Test 15: ACK STATUS!=0 -> immediate reject, no retransmit. */
static void test_send_reject_no_retry(void)
{
    cp_session_t s;
    uint8_t txbuf[32];
    size_t n = 0u;
    cp_session_init(&s, 0u);
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_send(&s, CP_CMD_DATA_REPORT,
                                      (const uint8_t *)"ab", 2u, 1000u,
                                      txbuf, sizeof(txbuf), &n));

    uint8_t ack[16];
    size_t alen = make_ack(ack, sizeof(ack), 0x00, CP_ACK_ERR_BAD_LEN);
    cp_frame_t out;
    size_t consumed = 0u;
    size_t ack_out = 0u;
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_feed(&s, ack, alen, 1100u, &out, &consumed,
                                      NULL, 0u, &ack_out));
    TEST_ASSERT_EQUAL_UINT8(0u, s.pending_active);
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_rejects);
    TEST_ASSERT_EQUAL_HEX8(0x01, s.tx_seq);           /* advances on reject */

    uint8_t tbuf[32];
    size_t tn = 0u;
    TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME,
                      cp_session_tick(&s, 1600u, tbuf, sizeof(tbuf), &tn));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_send_ack_ok_confirms);
    RUN_TEST(test_send_retransmit_on_deadline);
    RUN_TEST(test_send_fault_after_retry_max);
    RUN_TEST(test_send_reject_no_retry);
    return UNITY_END();
}
```

- [x] **Step 2: Run — verify RED**

Run: `cmake --build host/build && ctest --test-dir host/build -R coproc_session --output-on-failure`
Expected: builds and links; the four session tests FAIL (stubs return `CP_ERR_ARG`).

- [x] **Step 3: Implement the full session layer** — replace all stubs in `coproc_session.c`:

```c
#include "coproc_session.h"
#include <string.h>

static void emit_heartbeat(cp_session_t *s, uint32_t now_ms,
                           uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    cp_frame_t hb;
    hb.seq = 0u;
    hb.cmd = CP_CMD_HEARTBEAT;
    hb.len = 0u;
    size_t n = cp_encode(&hb, buf, buf_sz);
    if (n == 0u) {
        *out_len = 0u;
        return;
    }
    s->last_hb_ms = now_ms;
    *out_len = n;
}

static void emit_ack(uint8_t acked_seq, uint8_t status,
                     uint8_t *ack_buf, size_t ack_buf_sz, size_t *ack_len)
{
    if (ack_buf == NULL || ack_len == NULL) {
        return;
    }
    cp_frame_t ack;
    ack.seq = 0u;                          /* ACK header SEQ is always 0 */
    ack.cmd = CP_CMD_ACK;
    ack.len = 2u;
    ack.payload[0] = acked_seq;
    ack.payload[1] = status;
    size_t n = cp_encode(&ack, ack_buf, ack_buf_sz);
    *ack_len = (n > 0u) ? n : 0u;
}

static int cmd_is_known(uint8_t cmd)
{
    switch (cmd) {
    case CP_CMD_PING:
    case CP_CMD_DATA_REPORT:
    case CP_CMD_DIAG_REPORT:
    case CP_CMD_MQTT_PUB:
    case CP_CMD_MQTT_SUB:
    case CP_CMD_WIFI_CONFIG_SET:
    case CP_CMD_WIFI_CONFIG_GET:
    case CP_CMD_NET_STATUS:
    case CP_CMD_TIME_SET:
        return 1;
    default:
        return 0;
    }
}

void cp_session_init(cp_session_t *s, uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->last_peer_ms = now_ms;
    s->last_hb_ms   = now_ms;
    s->link         = CP_LINK_UP;
}

cp_status_t cp_session_send(cp_session_t *s, uint8_t cmd, const uint8_t *pl,
                            size_t len, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    if (s == NULL || buf == NULL || out_len == NULL) {
        return CP_ERR_ARG;
    }
    if (pl == NULL && len > 0u) {
        return CP_ERR_ARG;
    }
    if (len > CP_PAYLOAD_MAX) {
        return CP_ERR_LEN;
    }
    *out_len = 0u;
    if (s->pending_active) {
        return CP_ERR_BUSY;
    }
    if (cmd == CP_CMD_HEARTBEAT) {
        emit_heartbeat(s, now_ms, buf, buf_sz, out_len);
        return (*out_len > 0u) ? CP_OK : CP_ERR_BUF;
    }

    s->pending.seq = s->tx_seq;
    s->pending.cmd = cmd;
    s->pending.len = (uint16_t)len;
    if (len > 0u) {
        memcpy(s->pending.payload, pl, len);
    }
    s->pending_active = 1u;
    s->deadline_ms    = now_ms + CP_ACK_TIMEOUT_MS;
    s->retries        = 0u;

    size_t n = cp_encode(&s->pending, buf, buf_sz);
    if (n == 0u) {
        s->pending_active = 0u;
        return CP_ERR_BUF;
    }
    s->tx_frames++;
    *out_len = n;
    return CP_OK;
}

cp_status_t cp_session_tick(cp_session_t *s, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    if (s == NULL || buf == NULL || out_len == NULL) {
        return CP_ERR_ARG;
    }
    *out_len = 0u;

    /* 1. retransmit due (priority over heartbeat) */
    if (s->pending_active && now_ms >= s->deadline_ms) {
        if (s->retries < CP_RETRY_MAX) {
            s->retries++;
            s->tx_retries++;
            s->deadline_ms = now_ms + CP_ACK_TIMEOUT_MS;
            size_t n = cp_encode(&s->pending, buf, buf_sz);
            if (n == 0u) {
                return CP_ERR_BUF;
            }
            *out_len = n;
            return CP_OK;
        }
        /* retries exhausted -> FAULT: drop frame, link down */
        s->pending_active = 0u;
        s->tx_faults++;
        s->link = CP_LINK_DOWN;
        return CP_ERR_NO_FRAME;
    }

    /* 2. link supervision: peer silent too long */
    if (now_ms - s->last_peer_ms >= CP_LINK_DOWN_MS) {
        s->link = CP_LINK_DOWN;
    }

    /* 3. heartbeat probe */
    if (now_ms - s->last_hb_ms >= CP_HEARTBEAT_PERIOD_MS) {
        emit_heartbeat(s, now_ms, buf, buf_sz, out_len);
        return (*out_len > 0u) ? CP_OK : CP_ERR_NO_FRAME;
    }
    return CP_ERR_NO_FRAME;
}

cp_status_t cp_session_feed(cp_session_t *s, const uint8_t *data, size_t len,
                            uint32_t now_ms, cp_frame_t *out, size_t *consumed,
                            uint8_t *ack_buf, size_t ack_buf_sz, size_t *ack_len)
{
    if (s == NULL || data == NULL || consumed == NULL) {
        return CP_ERR_ARG;
    }
    if (ack_len != NULL) {
        *ack_len = 0u;
    }
    if (out != NULL) {
        out->cmd = 0u;                     /* "nothing delivered" sentinel */
    }

    cp_frame_t f;
    size_t used = 0u;
    cp_status_t st = cp_parse(&s->parser, data, len, &f, &used);
    *consumed = used;

    if (st == CP_ERR_NO_FRAME) {
        return CP_ERR_NO_FRAME;
    }
    if (st == CP_ERR_CRC) {
        s->rx_crc_err++;
        return CP_ERR_CRC;
    }
    if (st == CP_ERR_LEN) {
        s->rx_crc_err++;                   /* malformed header, same bucket */
        return CP_ERR_LEN;
    }
    if (st != CP_OK) {
        return st;
    }

    s->rx_frames++;
    s->last_peer_ms = now_ms;
    if (s->link == CP_LINK_DOWN) {
        s->link = CP_LINK_UP;
    }

    if (f.cmd == CP_CMD_ACK) {
        if (f.len == 2u && s->pending_active &&
            f.payload[0] == s->pending.seq) {
            s->pending_active = 0u;
            if (f.payload[1] == CP_ACK_OK) {
                s->tx_acked++;
            } else {
                s->tx_rejects++;           /* immediate fail, no retry */
            }
            s->tx_seq++;
        }
        return CP_OK;                      /* stale/foreign ACK: ignored */
    }

    if (f.cmd == CP_CMD_HEARTBEAT) {
        return CP_OK;                      /* probe: silent, refreshes peer */
    }

    if (!cmd_is_known(f.cmd)) {
        s->rx_nacks++;
        emit_ack(f.seq, CP_ACK_ERR_UNKNOWN_CMD, ack_buf, ack_buf_sz, ack_len);
        return CP_OK;
    }

    if (s->seq_seen && f.seq == s->last_good_seq) {
        s->rx_dup++;                       /* duplicate: no delivery */
    } else {
        s->last_good_seq = f.seq;
        s->seq_seen = 1u;
        if (out != NULL) {
            *out = f;                      /* deliver */
        }
    }
    emit_ack(f.seq, CP_ACK_OK, ack_buf, ack_buf_sz, ack_len);
    return CP_OK;
}
```

- [x] **Step 4: Run — verify GREEN (session subset)**

Run: `cmake --build host/build && ctest --test-dir host/build -R coproc_session --output-on-failure`
Expected: tests 12-15 PASS.

- [x] **Step 5: Commit**

```bash
git add stm32/middleware/coproc_proto/coproc_session.c host/tests/test_coproc_session.c
git commit -m "feat(m2.1): stop-and-wait session core (send/ACK/retransmit/fault)"
```

---

### Task 6: Session robustness — dedup / NACK / heartbeat / link / fuzz

**Files:**
- Modify: `host/tests/test_coproc_session.c` (append tests 16-20 and register)

**Interfaces:**
- Consumes: full session API (Task 5), `cp_encode` (Task 3).
- Produces: pinned evidence for the receiver path and link supervision — the behaviours M2.2's CommTask relies on for its health/diag reporting.

- [x] **Step 1: Append failing tests** (before `main` in `host/tests/test_coproc_session.c`)

```c
/* Build a DATA_REPORT frame via the verified encoder. */
static size_t make_data(uint8_t *buf, size_t buf_sz, uint8_t seq,
                        const char *pl, size_t plen)
{
    cp_frame_t f;
    memset(&f, 0, sizeof(f));
    f.seq = seq;
    f.cmd = CP_CMD_DATA_REPORT;
    f.len = (uint16_t)plen;
    memcpy(f.payload, pl, plen);
    return cp_encode(&f, buf, buf_sz);
}

/* Test 16: duplicate SEQ -> re-ACK, no delivery. */
static void test_rx_dedup_reacks(void)
{
    cp_session_t s;
    uint8_t frame[16];
    size_t flen = make_data(frame, sizeof(frame), 5u, "xy", 2u);
    cp_session_init(&s, 0u);

    cp_frame_t out;
    size_t consumed = 0u;
    uint8_t ack[16];
    size_t ack_len = 0u;
    cp_status_t st = cp_session_feed(&s, frame, flen, 100u, &out, &consumed,
                                     ack, sizeof(ack), &ack_len);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_DATA_REPORT, out.cmd);   /* delivered */
    TEST_ASSERT_EQUAL_HEX8(0x05, out.seq);
    TEST_ASSERT_EQUAL_UINT(10u, ack_len);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_ACK, ack[5]);
    TEST_ASSERT_EQUAL_HEX8(0x05, ack[6]);
    TEST_ASSERT_EQUAL_HEX8(CP_ACK_OK, ack[7]);

    st = cp_session_feed(&s, frame, flen, 200u, &out, &consumed,
                         ack, sizeof(ack), &ack_len);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(0u, out.cmd);                   /* not delivered */
    TEST_ASSERT_EQUAL_UINT32(1u, s.rx_dup);
    TEST_ASSERT_EQUAL_UINT(10u, ack_len);                  /* re-ACKed */
}

/* Test 17: 15 s peer silence -> link down. */
static void test_rx_link_down_after_silence(void)
{
    cp_session_t s;
    cp_session_init(&s, 0u);
    uint8_t tbuf[32];
    size_t tn = 0u;
    (void)cp_session_tick(&s, 14999u, tbuf, sizeof(tbuf), &tn);
    TEST_ASSERT_EQUAL(CP_LINK_UP, s.link);
    (void)cp_session_tick(&s, 15000u, tbuf, sizeof(tbuf), &tn);
    TEST_ASSERT_EQUAL(CP_LINK_DOWN, s.link);

    /* any valid frame revives the link */
    cp_frame_t out;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_feed(&s, GOLDEN_HB, sizeof(GOLDEN_HB),
                                      15100u, &out, &consumed,
                                      NULL, 0u, NULL));
    TEST_ASSERT_EQUAL(CP_LINK_UP, s.link);
}

/* Test 18: 1 KB pseudo-random garbage -> defined results only, then
 * normal operation resumes. */
static uint32_t g_xs;
static uint8_t xorshift_byte(void)
{
    g_xs ^= g_xs << 13;
    g_xs ^= g_xs >> 17;
    g_xs ^= g_xs << 5;
    return (uint8_t)g_xs;
}

static void test_rx_fuzz_survives(void)
{
    cp_session_t s;
    cp_session_init(&s, 0u);
    g_xs = 0x12345678u;                    /* fixed seed -> deterministic */

    static uint8_t chunk[7];
    for (size_t i = 0; i < 1024u / 7u; i++) {
        for (size_t j = 0; j < 7u; j++) {
            chunk[j] = xorshift_byte();
        }
        cp_frame_t out;
        size_t consumed = 0u;
        cp_status_t st = cp_session_feed(&s, chunk, sizeof(chunk), 1000u + i,
                                         &out, &consumed, NULL, 0u, NULL);
        TEST_ASSERT_TRUE(st == CP_OK || st == CP_ERR_NO_FRAME ||
                         st == CP_ERR_CRC || st == CP_ERR_LEN);
    }

    /* Drain: 520 zero bytes force any phantom frame to complete and
     * CRC-fail, deterministically resetting the parser. */
    static uint8_t drain[520];
    memset(drain, 0, sizeof(drain));
    cp_frame_t out;
    size_t consumed = 0u;
    (void)cp_session_feed(&s, drain, sizeof(drain), 2000u, &out, &consumed,
                          NULL, 0u, NULL);

    /* golden heartbeat is consumed silently */
    consumed = 0u;
    cp_status_t st = cp_session_feed(&s, GOLDEN_HB, sizeof(GOLDEN_HB), 2100u,
                                     &out, &consumed, NULL, 0u, NULL);
    TEST_ASSERT_TRUE(st == CP_OK || st == CP_ERR_CRC);
    TEST_ASSERT_EQUAL_HEX8(0u, out.cmd);

    /* a fresh data frame is delivered normally */
    uint8_t frame[16];
    size_t flen = make_data(frame, sizeof(frame), 9u, "ok", 2u);
    uint8_t ack[16];
    size_t ack_len = 0u;
    st = cp_session_feed(&s, frame, flen, 2200u, &out, &consumed,
                         ack, sizeof(ack), &ack_len);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_DATA_REPORT, out.cmd);
    TEST_ASSERT_EQUAL_UINT(10u, ack_len);
}

/* Test 19: heartbeat emission every 5 s. */
static void test_heartbeat_periodic(void)
{
    cp_session_t s;
    cp_session_init(&s, 0u);
    uint8_t tbuf[32];
    size_t tn = 0u;

    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 5000u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT(8u, tn);        /* empty HEARTBEAT frame */
    TEST_ASSERT_EQUAL_HEX8(CP_SOF0, tbuf[0]);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, tbuf[5]);

    TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME,
                      cp_session_tick(&s, 9999u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 10000u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT(8u, tn);
}

/* Test 20: unknown CMD with valid CRC -> NACK, not delivered. */
static void test_rx_unknown_cmd_nacks(void)
{
    cp_session_t s;
    cp_frame_t f;
    uint8_t frame[16];
    memset(&f, 0, sizeof(f));
    f.seq = 3u;
    f.cmd = 0x7Fu;                         /* not in the v1 CMD set */
    f.len = 1u;
    f.payload[0] = 0xAAu;
    size_t flen = cp_encode(&f, frame, sizeof(frame));
    TEST_ASSERT_TRUE(flen > 0u);

    cp_session_init(&s, 0u);
    cp_frame_t out;
    size_t consumed = 0u;
    uint8_t ack[16];
    size_t ack_len = 0u;
    cp_status_t st = cp_session_feed(&s, frame, flen, 100u, &out, &consumed,
                                     ack, sizeof(ack), &ack_len);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(0u, out.cmd);                   /* not delivered */
    TEST_ASSERT_EQUAL_UINT32(1u, s.rx_nacks);
    TEST_ASSERT_EQUAL_UINT(10u, ack_len);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_ACK, ack[5]);
    TEST_ASSERT_EQUAL_HEX8(0x03, ack[6]);                  /* acked SEQ 3 */
    TEST_ASSERT_EQUAL_HEX8(CP_ACK_ERR_UNKNOWN_CMD, ack[7]);
}
```

Register in `main` before `return UNITY_END();`:

```c
    RUN_TEST(test_rx_dedup_reacks);
    RUN_TEST(test_rx_link_down_after_silence);
    RUN_TEST(test_rx_fuzz_survives);
    RUN_TEST(test_heartbeat_periodic);
    RUN_TEST(test_rx_unknown_cmd_nacks);
```

- [x] **Step 2: Run — verify RED, then GREEN**

Run: `cmake --build host/build && ctest --test-dir host/build -R coproc_session --output-on-failure`
Expected: Task 5 already implemented the behaviours these tests exercise, so they are expected to pass immediately; if any FAILS, fix `coproc_session.c` (a real bug caught) and re-run. Final state: tests 12-20 all PASS.

- [x] **Step 3: Run the whole host suite and the firmware build**

Run: `ctest --test-dir host/build --output-on-failure && TMPDIR=/home/eli_peng/tmp make -C /mnt/d/InduLogger/stm32 -j4`
Expected: all suites green (including pre-existing M1 tests); firmware builds clean.

- [x] **Step 4: Commit**

```bash
git add host/tests/test_coproc_session.c stm32/middleware/coproc_proto/coproc_session.c
git commit -m "feat(m2.1): session robustness (dedup, NACK, heartbeat, link, fuzz)"
```

---

### Task 7: Protocol specification (English deliverable)

**Files:**
- Create: `docs/specs/coproc-protocol-spec.md`

**Interfaces:**
- Consumes: the frozen design section of this plan (single source of truth).
- Produces: the portfolio deliverable promised in spec §15 item "独立协议规格书（英文）"; M2.2+ implementers and the ESP32-side team read this instead of reverse-engineering code.

- [x] **Step 1: Write the document** with exactly these sections (content = the Frozen Design section above, re-expressed as a standalone spec; do not invent new rules):

1. **Overview** — purpose (STM32↔ESP32-S3 UART link), byte-oriented, big-endian, library location `stm32/middleware/coproc_proto/` (same sources on both sides).
2. **Frame format** — offset table (SOF0/SOF1/LEN_H/LEN_L/SEQ/CMD/PAYLOAD/CRC_H/CRC_L), LEN 0..512, total length 8+N, `CP_FRAME_MAX 520`.
3. **CRC** — CRC-16/CCITT-False, poly 0x1021, init 0xFFFF, no reflection, xorout 0; coverage offset 2..5+N; worked examples: `check("123456789")=0x29B1`, heartbeat `A5 5A 00 00 00 02 A4 82`.
4. **Command set** — the v1 CMD table (codes, direction, payload layout, ACK expectation): PING (echo payload, ACK), HEARTBEAT (empty, no ACK), DATA_REPORT/DIAG_REPORT (STM32→ESP), MQTT_PUB/MQTT_SUB (both), WIFI_CONFIG_SET/GET (STM32→ESP), NET_STATUS (ESP→STM), TIME_SET unix-time 4 B big-endian (**ESP32→STM32**), ACK ([SEQ,STATUS], header SEQ fixed 0, never ACKed/deduped/delivered). OTA group reserved for v2.
5. **ACK and error semantics** — STATUS table (OK/ERR_UNKNOWN_CMD/ERR_BAD_LEN/ERR_BAD_ARG); STATUS=0 confirms; STATUS≠0 = immediate failure, no retry, TX SEQ still advances; CRC error = silent drop; over-length LEN = drop at LEN_L; unknown CMD = NACK 0x01.
6. **Session behaviour** — stop-and-wait sender (500 ms deadline, 3 retries, same SEQ on retransmit, FAULT after the 3rd missed deadline, `CP_ERR_BUSY` while pending); receiver dedup (first frame always delivered, repeat of `last_good_seq` re-ACKed not delivered, SEQ wraps from 0); heartbeat 5 s fire-and-forget; link DOWN after 15 s silence, revived by any valid frame.
7. **State machine diagrams** — two mermaid `stateDiagram-v2` blocks (sender: IDLE→WAIT_ACK→(RETRANS|FAULT); receiver: HUNT→HDR→PAYLOAD→CRC with resync edges).
8. **Timing diagrams** — two mermaid `sequenceDiagram` blocks: (a) successful transfer with ACK, (b) loss → retransmit → FAULT.
9. **Tunables** — the five `CP_*` constants with defaults and the note that M2.2 may retune values without structural changes.
10. **Reference** — implementation `stm32/middleware/coproc_proto/`, host tests `host/tests/test_coproc_proto.c` / `test_coproc_session.c` (20 tests, golden vectors).

- [x] **Step 2: Self-check the spec against the library**

Run: `grep -n "0x29B1\|A4 82\|A482" docs/specs/coproc-protocol-spec.md && ctest --test-dir host/build -R coproc --output-on-failure`
Expected: golden vectors present; all 20 tests still green (spec claims match tested behaviour).

- [x] **Step 3: Commit**

```bash
git add docs/specs/coproc-protocol-spec.md
git commit -m "docs(m2.1): coprocessor protocol specification (deliverable)"
```

---

### Task 8: Closure — AGENTS.md, optional MISRA check, push, CI verify

**Files:**
- Modify: `AGENTS.md` (§1 status line, §3 progress table, §10 item 1)
- Modify: `docs/plans/2026-11-20-m2-1-coproc-proto-tdd.md` (tick all checkboxes)

**Interfaces:**
- Consumes: everything above, green.
- Produces: pushed branch with CI green; AGENTS.md reflects M2.1 done.

- [x] **Step 1: Optional MISRA addon check** (user ruling: wire it if available, otherwise the deferral already recorded in AGENTS.md §10 stands)

```bash
find /home/eli_peng -name "misra*.py" -o -name "misra.c" 2>/dev/null | grep -i cppcheck | head -5
```

If a cppcheck addon directory exists: wire `--addon=misra` into the ci.yml cppcheck step and fix any reported findings in the new middleware files. If not: do nothing here (AGENTS.md §10 item 5 already records the deferral).

- [x] **Step 2: Update AGENTS.md** — three precise edits:
  - §1 status line becomes: `**M1 已完成收官（tag m1-skeleton、合 main）；M2.1 协议库已实现（coproc_proto 主机 TDD 20/20 绿 + CI 绿）；v1 范围已定方案 A 全收（2026-11-20 缺口盘点，v1 出货点顺延至 W6 初）**`
  - §3 progress table gains a row: `| M2.1 | 协议库 coproc_proto/coproc_session 主机 TDD（20 测试）+ 构建接线 + 协议规格书 | ✓ <commit-of-task-6> |`
  - §10 item 1 gets the suffix `—— ✅ 已完成（见 §3 M2.1 行；协议参数冻结见第 6 条）`

- [x] **Step 3: Tick every checkbox in this plan file** (`- [ ]` → `- [x]`), commit

```bash
git add AGENTS.md docs/plans/2026-11-20-m2-1-coproc-proto-tdd.md
git commit -m "chore(m2.1): closure - AGENTS update, plan checkboxes ticked"
```

- [x] **Step 4: Push both remotes and verify CI**

```bash
git push origin refs/heads/m2-coproc && git push gitee refs/heads/m2-coproc
curl -s https://api.github.com/repos/EliPeng-sh/InduLogger/actions/runs?per_page=1
```

Expected: push succeeds (retry on transient GitHub errors, do not change config); the latest run is for `m2-coproc`; poll the `conclusion` field until it is `success` (three jobs: host-tests, firmware-build, static-analysis). If `static-analysis` flags anything in the new middleware sources, fix and re-push.

- [x] **Step 5: Report** — summarize commits, test count, CI conclusion to the user (in Chinese).

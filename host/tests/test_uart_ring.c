/* Host TDD for bsp/uart_ring (M2.2 Task 3).
 * Pure ring-drain semantics per docs/specs/2026-11-25-m2-2-uart-link-design.md §4.1. */
#include <string.h>

#include "unity.h"

#include "bsp/uart_ring/uart_ring.h"
#include "coproc_proto.h"

#define RING_SIZE 1024u

static uint8_t     s_buf[RING_SIZE];
static uart_ring_t s_ring;
static uint8_t     s_dst[RING_SIZE];

void setUp(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    memset(s_dst, 0, sizeof(s_dst));
    uring_init(&s_ring, s_buf, (uint16_t)RING_SIZE);
}

void tearDown(void) {}

/* wr == rd => nothing readable, no overrun. */
static void test_drain_empty(void)
{
    bool   overrun = true;
    size_t n = uring_drain(&s_ring, 0u, s_dst, sizeof(s_dst), &overrun);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)n);
    TEST_ASSERT_FALSE(overrun);
    TEST_ASSERT_EQUAL_UINT16(0u, s_ring.rd);
}

/* Straightforward in-band span: bytes copied in order, rd advances. */
static void test_drain_normal_span(void)
{
    for (uint16_t i = 0u; i < 5u; i++) {
        s_buf[i] = (uint8_t)(0x10u + i);
    }
    bool   overrun = true;
    size_t n = uring_drain(&s_ring, 5u, s_dst, sizeof(s_dst), &overrun);
    TEST_ASSERT_EQUAL_UINT32(5u, (uint32_t)n);
    TEST_ASSERT_FALSE(overrun);
    for (uint16_t i = 0u; i < 5u; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x10u + i, s_dst[i]);
    }
    TEST_ASSERT_EQUAL_UINT16(5u, s_ring.rd);

    /* Draining the same window again yields nothing. */
    n = uring_drain(&s_ring, 5u, s_dst, sizeof(s_dst), &overrun);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)n);
    TEST_ASSERT_FALSE(overrun);
}

/* rd=1020, wr=8, size=1024 => span 12 delivered across the wrap,
 * byte order preserved (tail segment first, then head segment). */
static void test_drain_wrap_split(void)
{
    s_ring.rd = 1020u;
    for (uint16_t i = 0u; i < 4u; i++) {
        s_buf[1020u + i] = (uint8_t)(0xA0u + i);
    }
    for (uint16_t i = 0u; i < 8u; i++) {
        s_buf[i] = (uint8_t)(0xB0u + i);
    }
    bool   overrun = true;
    size_t n = uring_drain(&s_ring, 8u, s_dst, sizeof(s_dst), &overrun);
    TEST_ASSERT_EQUAL_UINT32(12u, (uint32_t)n);
    TEST_ASSERT_FALSE(overrun);
    for (uint16_t i = 0u; i < 12u; i++) {
        uint8_t expect = (i < 4u) ? (uint8_t)(0xA0u + i) : (uint8_t)(0xB0u + (i - 4u));
        TEST_ASSERT_EQUAL_UINT8(expect, s_dst[i]);
    }
    TEST_ASSERT_EQUAL_UINT16(8u, s_ring.rd);
}

/* max < span => partial copy, rd advances by max, rest stays readable. */
static void test_drain_max_clamp(void)
{
    for (uint16_t i = 0u; i < 10u; i++) {
        s_buf[i] = (uint8_t)i;
    }
    bool   overrun = true;
    size_t n = uring_drain(&s_ring, 10u, s_dst, 4u, &overrun);
    TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)n);
    TEST_ASSERT_FALSE(overrun);
    TEST_ASSERT_EQUAL_UINT16(4u, s_ring.rd);
    for (uint16_t i = 0u; i < 4u; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, s_dst[i]); /* bytes 0..3 */
    }

    n = uring_drain(&s_ring, 10u, s_dst, sizeof(s_dst), &overrun);
    TEST_ASSERT_EQUAL_UINT32(6u, (uint32_t)n); /* remainder 4..9 */
    TEST_ASSERT_FALSE(overrun);
    TEST_ASSERT_EQUAL_UINT16(10u, s_ring.rd);
    for (uint16_t i = 0u; i < 6u; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(4u + i), s_dst[i]);
    }
}

/* span = size/2 + 1 => writer lapped reader: report overrun, resync rd,
 * copy nothing. */
static void test_overrun_wr_laps_rd(void)
{
    bool   overrun = false;
    size_t n = uring_drain(&s_ring, RING_SIZE / 2u + 1u, s_dst, sizeof(s_dst),
                           &overrun);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)n);
    TEST_ASSERT_TRUE(overrun);
    TEST_ASSERT_EQUAL_UINT16(RING_SIZE / 2u + 1u, s_ring.rd);
}

/* span == size/2 exactly is NOT an overrun — pins the threshold. */
static void test_boundary_half(void)
{
    bool   overrun = true;
    size_t n = uring_drain(&s_ring, RING_SIZE / 2u, s_dst, sizeof(s_dst), &overrun);
    TEST_ASSERT_EQUAL_UINT32(RING_SIZE / 2u, (uint32_t)n);
    TEST_ASSERT_FALSE(overrun);
    TEST_ASSERT_EQUAL_UINT16(RING_SIZE / 2u, s_ring.rd);
}

/* Review Focus 1: a 20-byte golden frame written by the (emulated) DMA
 * writer in five uneven chunks, pulled by the reader in five uneven
 * drains, must reassemble bit-exact and parse once via cp_parse. */
static void test_chunked_stream_reassembly(void)
{
    /* Build golden frame: seq 7, PING, 12-byte payload => 20 bytes total. */
    cp_frame_t f;
    memset(&f, 0, sizeof(f));
    f.seq = 7u;
    f.cmd = CP_CMD_PING;
    f.len = 12u;
    for (uint16_t i = 0u; i < 12u; i++) {
        f.payload[i] = (uint8_t)(0x40u + i);
    }
    uint8_t frame[CP_FRAME_MAX];
    size_t  flen = cp_encode(&f, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT32(20u, (uint32_t)flen);

    /* Writer: five uneven chunks into the ring (wr starts at 0, no wrap). */
    const size_t wchunks[] = {3u, 9u, 1u, 5u, 2u};
    uint16_t     wr = 0u;
    size_t       src_off = 0u;
    for (size_t c = 0u; c < 5u; c++) {
        for (size_t i = 0u; i < wchunks[c]; i++) {
            s_buf[wr] = frame[src_off + i];
            wr = (uint16_t)((wr + 1u) % RING_SIZE);
        }
        src_off += wchunks[c];
    }
    TEST_ASSERT_EQUAL_UINT16(20u, wr);

    /* Reader: five uneven pulls through uring_drain. The writer cursor for
     * each pull is the final wr (DMA keeps writing "ahead" in this model);
     * we clamp with per-call max. */
    const size_t rchunks[] = {5u, 4u, 2u, 7u, 2u};
    uint8_t      got[64];
    size_t       got_n = 0u;
    for (size_t c = 0u; c < 5u; c++) {
        bool   overrun = true;
        size_t n = uring_drain(&s_ring, wr, got + got_n, rchunks[c], &overrun);
        TEST_ASSERT_FALSE(overrun);
        TEST_ASSERT_EQUAL_UINT32(rchunks[c], (uint32_t)n);
        got_n += n;
    }
    TEST_ASSERT_EQUAL_UINT32(20u, (uint32_t)got_n);

    /* Parser sees the frame exactly once over the reassembled stream. */
    cp_parser_t p;
    cp_parser_init(&p);
    cp_frame_t  out;
    size_t      consumed = 0u;
    cp_status_t st = cp_parse(&p, got, got_n, &out, &consumed);
    TEST_ASSERT_EQUAL_INT(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT32(20u, (uint32_t)consumed);
    TEST_ASSERT_EQUAL_UINT8(7u, out.seq);
    TEST_ASSERT_EQUAL_UINT8(CP_CMD_PING, out.cmd);
    TEST_ASSERT_EQUAL_UINT16(12u, out.len);
    TEST_ASSERT_EQUAL_MEMORY(&f.payload, &out.payload, 12u);

    /* Nothing left behind. */
    st = cp_parse(&p, got + consumed, 0u, &out, &consumed);
    TEST_ASSERT_EQUAL_INT(CP_ERR_NO_FRAME, st);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_drain_empty);
    RUN_TEST(test_drain_normal_span);
    RUN_TEST(test_drain_wrap_split);
    RUN_TEST(test_drain_max_clamp);
    RUN_TEST(test_overrun_wr_laps_rd);
    RUN_TEST(test_boundary_half);
    RUN_TEST(test_chunked_stream_reassembly);
    return UNITY_END();
}

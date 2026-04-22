#include "bench_common.h"

#if !defined(__aarch64__)
#error "bench_arm_sve.c requires AArch64"
#endif

#include <arm_sve.h>

enum {
  SVE_MAX_BYTES = 256,
};

typedef struct BENCH_ALIGN(64) sve_ctx {
  size_t active_bytes;
  size_t scalar_mask_bytes;
  uint8_t lhs_a[SVE_MAX_BYTES];
  uint8_t rhs_a[SVE_MAX_BYTES];
  uint8_t lhs_b[SVE_MAX_BYTES];
  uint8_t rhs_b[SVE_MAX_BYTES];
  uint8_t data[SVE_MAX_BYTES];
  uint8_t positions_out[SVE_MAX_BYTES];
  uint8_t selected_out[SVE_MAX_BYTES];
  uint64_t spill_words[4];
} sve_ctx_t;

static volatile uint64_t g_sink = 0;

static void sve_ctx_init(sve_ctx_t *ctx) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->active_bytes = svcntb();
  if (ctx->active_bytes > SVE_MAX_BYTES) {
    fprintf(stderr, "svcntb()=%zu exceeds SVE_MAX_BYTES=%d\n",
            ctx->active_bytes,
            SVE_MAX_BYTES);
    exit(1);
  }
  ctx->scalar_mask_bytes = ctx->active_bytes > 64 ? 64 : ctx->active_bytes;

  bench_fill_bytes(ctx->lhs_a, ctx->active_bytes, 0x11001100);
  bench_fill_bytes(ctx->rhs_a, ctx->active_bytes, 0x22002200);
  bench_fill_bytes(ctx->lhs_b, ctx->active_bytes, 0x33003300);
  bench_fill_bytes(ctx->rhs_b, ctx->active_bytes, 0x44004400);
  bench_fill_bytes(ctx->data, ctx->active_bytes, 0x55005500);

  for (size_t i = 0; i < ctx->active_bytes; ++i) {
    if ((i % 3U) == 0U || (i % 7U) == 1U) {
      ctx->rhs_a[i] = ctx->lhs_a[i];
    } else {
      ctx->rhs_a[i] ^= 0x3cU;
    }

    if ((i % 5U) == 0U || (i % 11U) == 2U) {
      ctx->rhs_b[i] = ctx->lhs_b[i];
    } else {
      ctx->rhs_b[i] ^= 0xc3U;
    }
  }
}

static BENCH_NOINLINE uint64_t bench_empty(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  return ctx->active_bytes ^ ctx->scalar_mask_bytes;
}

static BENCH_NOINLINE uint64_t sve_str_ldr_mask(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  uint64_t mask = 0;
  __asm__ volatile(
      "whilelt p0.b, xzr, %[n]\n\t"
      "ld1b { z0.b }, p0/z, [%[lhs]]\n\t"
      "ld1b { z1.b }, p0/z, [%[rhs]]\n\t"
      "cmpeq p1.b, p0/z, z0.b, z1.b\n\t"
      "str p1, [%[spill]]\n\t"
      "ldr %[mask], [%[spill]]\n\t"
      : [mask] "=&r"(mask)
      : [lhs] "r"(ctx->lhs_a),
        [rhs] "r"(ctx->rhs_a),
        [spill] "r"(ctx->spill_words),
        [n] "r"(ctx->scalar_mask_bytes)
      : "p0", "p1", "z0", "z1", "cc", "memory");
  return mask;
}

#if defined(ENABLE_SVE2P1)
static BENCH_NOINLINE uint64_t sve2p1_pmov_umov_mask(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  uint64_t mask = 0;
  __asm__ volatile(
      "whilelt p0.b, xzr, %[n]\n\t"
      "ld1b { z0.b }, p0/z, [%[lhs]]\n\t"
      "ld1b { z1.b }, p0/z, [%[rhs]]\n\t"
      "cmpeq p1.b, p0/z, z0.b, z1.b\n\t"
      "pmov z2, p1.b\n\t"
      "umov %x[mask], v2.d[0]\n\t"
      : [mask] "=&r"(mask)
      : [lhs] "r"(ctx->lhs_a),
        [rhs] "r"(ctx->rhs_a),
        [n] "r"(ctx->scalar_mask_bytes)
      : "p0", "p1", "z0", "z1", "z2", "cc", "memory");
  return mask;
}
#endif

static BENCH_NOINLINE uint64_t sve_any_all_count(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  svbool_t pg = svwhilelt_b8((uint64_t)0, ctx->active_bytes);
  svuint8_t lhs = svld1_u8(pg, ctx->lhs_a);
  svuint8_t rhs = svld1_u8(pg, ctx->rhs_a);
  svbool_t pred = svcmpeq_u8(pg, lhs, rhs);
  uint64_t any = svptest_any(pg, pred) ? 1U : 0U;
  uint64_t count = svcntp_b8(pg, pred);
  uint64_t all = count == ctx->active_bytes;
  return any | (all << 1U) | (count << 2U);
}

static BENCH_NOINLINE uint64_t sve_first_hit(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  svbool_t pg = svwhilelt_b8((uint64_t)0, ctx->active_bytes);
  svuint8_t lhs = svld1_u8(pg, ctx->lhs_a);
  svuint8_t rhs = svld1_u8(pg, ctx->rhs_a);
  svbool_t pred = svcmpeq_u8(pg, lhs, rhs);
  if (!svptest_any(pg, pred)) {
    return UINT64_MAX;
  }
  svbool_t first = svpfirst_b(pg, pred);
  svbool_t before = svbrkb_b_z(pg, first);
  return svcntp_b8(pg, before);
}

#if defined(ENABLE_SVE2P2)
static BENCH_NOINLINE uint64_t sve2p2_first_hit(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  uint64_t index = UINT64_MAX;
  __asm__ volatile(
      "whilelt p0.b, xzr, %[n]\n\t"
      "ld1b { z0.b }, p0/z, [%[lhs]]\n\t"
      "ld1b { z1.b }, p0/z, [%[rhs]]\n\t"
      "cmpeq p1.b, p0/z, z0.b, z1.b\n\t"
      "ptest p0, p1.b\n\t"
      "b.eq 1f\n\t"
      "firstp %x[idx], p0, p1.b\n\t"
      "b 2f\n"
      "1:\n\t"
      "mov %x[idx], #-1\n"
      "2:\n\t"
      : [idx] "=&r"(index)
      : [lhs] "r"(ctx->lhs_a),
        [rhs] "r"(ctx->rhs_a),
        [n] "r"(ctx->active_bytes)
      : "p0", "p1", "z0", "z1", "cc", "memory");
  return index;
}
#endif

static BENCH_NOINLINE uint64_t sve_all_hit_positions(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  svbool_t pg = svwhilelt_b8((uint64_t)0, ctx->active_bytes);
  svuint8_t lhs = svld1_u8(pg, ctx->lhs_a);
  svuint8_t rhs = svld1_u8(pg, ctx->rhs_a);
  svbool_t pred = svcmpeq_u8(pg, lhs, rhs);

  size_t count = 0;
  if (svptest_any(pg, pred)) {
    svbool_t cursor = svpfirst_b(pg, pred);
    while (svptest_any(pg, cursor)) {
      svbool_t before = svbrkb_b_z(pg, cursor);
      ctx->positions_out[count++] = (uint8_t)svcntp_b8(pg, before);
      cursor = svpnext_b8(pg, cursor);
    }
  }

  return bench_checksum_bytes(ctx->positions_out, count) ^ count;
}

#if defined(ENABLE_SVE2P2)
static BENCH_NOINLINE uint64_t sve2p2_all_hit_positions(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  uint64_t count = 0;
  __asm__ volatile(
      "whilelt p0.b, xzr, %[n]\n\t"
      "ld1b { z2.b }, p0/z, [%[lhs]]\n\t"
      "ld1b { z3.b }, p0/z, [%[rhs]]\n\t"
      "cmpeq p1.b, p0/z, z2.b, z3.b\n\t"
      "index z0.b, #0, #1\n\t"
      "compact z1.b, p1, z0.b\n\t"
      "cntp %x[count], p0, p1.b\n\t"
      "whilelt p2.b, xzr, %x[count]\n\t"
      "st1b { z1.b }, p2, [%[out]]\n\t"
      : [count] "=&r"(count)
      : [lhs] "r"(ctx->lhs_a),
        [rhs] "r"(ctx->rhs_a),
        [out] "r"(ctx->positions_out),
        [n] "r"(ctx->active_bytes)
      : "p0", "p1", "p2", "z0", "z1", "z2", "z3", "cc", "memory");

  return bench_checksum_bytes(ctx->positions_out, (size_t)count) ^ count;
}
#endif

static BENCH_NOINLINE uint64_t sve_selected_data(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  svbool_t pg = svwhilelt_b8((uint64_t)0, ctx->active_bytes);
  svuint8_t lhs = svld1_u8(pg, ctx->lhs_a);
  svuint8_t rhs = svld1_u8(pg, ctx->rhs_a);
  svuint8_t data = svld1_u8(pg, ctx->data);
  svbool_t pred = svcmpeq_u8(pg, lhs, rhs);

  size_t count = 0;
  if (svptest_any(pg, pred)) {
    svbool_t cursor = svpfirst_b(pg, pred);
    while (svptest_any(pg, cursor)) {
      ctx->selected_out[count++] = svlastb_u8(cursor, data);
      cursor = svpnext_b8(pg, cursor);
    }
  }

  return bench_checksum_bytes(ctx->selected_out, count) ^ count;
}

#if defined(ENABLE_SVE2P2)
static BENCH_NOINLINE uint64_t sve2p2_selected_data(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  uint64_t count = 0;
  __asm__ volatile(
      "whilelt p0.b, xzr, %[n]\n\t"
      "ld1b { z2.b }, p0/z, [%[lhs]]\n\t"
      "ld1b { z3.b }, p0/z, [%[rhs]]\n\t"
      "ld1b { z4.b }, p0/z, [%[data]]\n\t"
      "cmpeq p1.b, p0/z, z2.b, z3.b\n\t"
      "compact z0.b, p1, z4.b\n\t"
      "cntp %x[count], p0, p1.b\n\t"
      "whilelt p2.b, xzr, %x[count]\n\t"
      "st1b { z0.b }, p2, [%[out]]\n\t"
      : [count] "=&r"(count)
      : [lhs] "r"(ctx->lhs_a),
        [rhs] "r"(ctx->rhs_a),
        [data] "r"(ctx->data),
        [out] "r"(ctx->selected_out),
        [n] "r"(ctx->active_bytes)
      : "p0", "p1", "p2", "z0", "z2", "z3", "z4", "cc", "memory");

  return bench_checksum_bytes(ctx->selected_out, (size_t)count) ^ count;
}
#endif

static BENCH_NOINLINE uint64_t sve_predicate_algebra(void *opaque) {
  sve_ctx_t *ctx = (sve_ctx_t *)opaque;
  svbool_t pg = svwhilelt_b8((uint64_t)0, ctx->active_bytes);
  svuint8_t lhs_a = svld1_u8(pg, ctx->lhs_a);
  svuint8_t rhs_a = svld1_u8(pg, ctx->rhs_a);
  svuint8_t lhs_b = svld1_u8(pg, ctx->lhs_b);
  svuint8_t rhs_b = svld1_u8(pg, ctx->rhs_b);

  svbool_t pa = svcmpeq_u8(pg, lhs_a, rhs_a);
  svbool_t pb = svcmpeq_u8(pg, lhs_b, rhs_b);
  svbool_t pand = svand_b_z(pg, pa, pb);
  svbool_t por = svorr_b_z(pg, pa, pb);
  svbool_t before = svbrkb_b_z(pg, pand);
  svbool_t after = svbrka_b_z(pg, pand);

  return svcntp_b8(pg, pand) ^
         (svcntp_b8(pg, por) << 8U) ^
         (svcntp_b8(pg, before) << 16U) ^
         (svcntp_b8(pg, after) << 24U);
}

static void run_suite(const char *suite,
                      sve_ctx_t *ctx,
                      const bench_case_t *cases,
                      size_t count,
                      uint64_t iters,
                      int rounds) {
  uint64_t freq = bench_cycle_frequency();
  bench_print_banner(suite, freq);
  uint64_t baseline = bench_measure(bench_empty, ctx, iters, rounds, &g_sink);
  for (size_t i = 0; i < count; ++i) {
    uint64_t cycles = bench_measure(cases[i].fn, ctx, iters, rounds, &g_sink);
    bench_print_result(cases[i].name, cycles, iters, baseline, g_sink);
  }
}

int main(int argc, char **argv) {
  uint64_t iters = argc > 1 ? bench_parse_u64(argv[1], 1000000) : 1000000;
  int rounds = argc > 2 ? (int)bench_parse_u64(argv[2], 7) : 7;

  sve_ctx_t ctx;
  sve_ctx_init(&ctx);

  const bench_case_t cases[] = {
      {"5.3_sve_str_ldr_mask", sve_str_ldr_mask},
#if defined(ENABLE_SVE2P1)
      {"5.4_sve2p1_pmov_umov_mask", sve2p1_pmov_umov_mask},
#endif
      {"7.1_sve_any_all_count", sve_any_all_count},
      {"7.2_sve_first_hit", sve_first_hit},
#if defined(ENABLE_SVE2P2)
      {"7.2_sve2p2_first_hit", sve2p2_first_hit},
#endif
      {"7.3_sve_all_hit_positions", sve_all_hit_positions},
#if defined(ENABLE_SVE2P2)
      {"7.3_sve2p2_all_hit_positions", sve2p2_all_hit_positions},
#endif
      {"7.4_sve_selected_data", sve_selected_data},
#if defined(ENABLE_SVE2P2)
      {"7.4_sve2p2_selected_data", sve2p2_selected_data},
#endif
      {"7.5_sve_predicate_algebra", sve_predicate_algebra},
  };

  run_suite(
#if defined(ENABLE_SVE2P2)
      "arm-sve2p2",
#elif defined(ENABLE_SVE2P1)
      "arm-sve2p1",
#else
      "arm-sve",
#endif
      &ctx,
      cases,
      sizeof(cases) / sizeof(cases[0]),
      iters,
      rounds);

  if (g_sink == 0xdeadbeefULL) {
    fprintf(stderr, "sink=%" PRIu64 "\n", g_sink);
  }
  return 0;
}

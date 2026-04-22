#include "bench_common.h"

#if !defined(__aarch64__)
#error "bench_arm_scalar.c requires AArch64"
#endif

#include <arm_neon.h>

enum {
  ARM_SCALAR_BYTES = 64,
  NEON_MASK_BYTES = 16,
};

typedef struct BENCH_ALIGN(64) arm_scalar_ctx {
  uint8_t lhs_a[ARM_SCALAR_BYTES];
  uint8_t rhs_a[ARM_SCALAR_BYTES];
  uint8_t lhs_b[ARM_SCALAR_BYTES];
  uint8_t rhs_b[ARM_SCALAR_BYTES];
  uint8_t data[ARM_SCALAR_BYTES];
  uint8_t positions_out[ARM_SCALAR_BYTES];
  uint8_t selected_out[ARM_SCALAR_BYTES];
  uint64_t mask_a;
  uint64_t mask_b;
  uint64_t extra_mask;
  uint64_t all_mask;
  uint8_t neon_weights[NEON_MASK_BYTES];
} arm_scalar_ctx_t;

static volatile uint64_t g_sink = 0;

static inline uint64_t a64_first_bit_from_mask(uint64_t mask) {
  uint64_t index;
  __asm__ volatile(
      "rbit %0, %1\n\t"
      "clz  %0, %0\n\t"
      : "=&r"(index)
      : "r"(mask));
  return index;
}

#if defined(ENABLE_CSSC)
static inline uint64_t cssc_ctz_u64(uint64_t mask) {
  uint64_t index;
  __asm__ volatile("ctz %0, %1" : "=&r"(index) : "r"(mask));
  return index;
}

static inline uint64_t cssc_cnt_u64(uint64_t mask) {
  uint64_t count;
  __asm__ volatile("cnt %0, %1" : "=&r"(count) : "r"(mask));
  return count;
}
#endif

static void arm_scalar_ctx_init(arm_scalar_ctx_t *ctx) {
  memset(ctx, 0, sizeof(*ctx));
  bench_fill_bytes(ctx->lhs_a, ARM_SCALAR_BYTES, 0x1001);
  bench_fill_bytes(ctx->rhs_a, ARM_SCALAR_BYTES, 0x2002);
  bench_fill_bytes(ctx->lhs_b, ARM_SCALAR_BYTES, 0x3003);
  bench_fill_bytes(ctx->rhs_b, ARM_SCALAR_BYTES, 0x4004);
  bench_fill_bytes(ctx->data, ARM_SCALAR_BYTES, 0x5005);

  for (size_t i = 0; i < ARM_SCALAR_BYTES; ++i) {
    if ((i % 3U) == 0U || (i % 7U) == 1U || i == 63U) {
      ctx->rhs_a[i] = ctx->lhs_a[i];
    } else {
      ctx->rhs_a[i] ^= 0x5aU;
    }

    if ((i % 5U) == 0U || (i % 11U) == 2U) {
      ctx->rhs_b[i] = ctx->lhs_b[i];
    } else {
      ctx->rhs_b[i] ^= 0xa5U;
    }
  }

  for (size_t i = 0; i < 8; ++i) {
    ctx->neon_weights[i] = (uint8_t)(1U << i);
    ctx->neon_weights[i + 8] = (uint8_t)(1U << i);
  }

  ctx->mask_a = bench_scalar_mask_from_bytes(ctx->lhs_a, ctx->rhs_a, ARM_SCALAR_BYTES);
  ctx->mask_b = bench_scalar_mask_from_bytes(ctx->lhs_b, ctx->rhs_b, ARM_SCALAR_BYTES);
  ctx->extra_mask = UINT64_C(0x9249249249249249);
  ctx->all_mask = bench_mask_all(ARM_SCALAR_BYTES);
}

static BENCH_NOINLINE uint64_t bench_empty(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  return ctx->mask_a ^ ctx->mask_b ^ ctx->extra_mask;
}

static BENCH_NOINLINE uint64_t neon_to_scalar_mask(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  uint8x16_t lhs = vld1q_u8(ctx->lhs_a);
  uint8x16_t rhs = vld1q_u8(ctx->rhs_a);
  uint8x16_t cmp = vceqq_u8(lhs, rhs);
  uint8x16_t weighted = vandq_u8(cmp, vld1q_u8(ctx->neon_weights));
  uint8_t low = vaddv_u8(vget_low_u8(weighted));
  uint8_t high = vaddv_u8(vget_high_u8(weighted));
  return (uint64_t)low | ((uint64_t)high << 8U);
}

static BENCH_NOINLINE uint64_t a64_any_all_count(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  uint64_t mask = ctx->mask_a;
  uint64_t any = mask != 0;
  uint64_t all = mask == ctx->all_mask;
  uint64_t count = bench_popcount_loop_u64(mask);
  return any | (all << 1U) | (count << 2U);
}

#if defined(ENABLE_CSSC)
static BENCH_NOINLINE uint64_t cssc_any_all_count(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  uint64_t mask = ctx->mask_a;
  uint64_t any = mask != 0;
  uint64_t all = mask == ctx->all_mask;
  uint64_t count = cssc_cnt_u64(mask);
  return any | (all << 1U) | (count << 2U);
}
#endif

static BENCH_NOINLINE uint64_t a64_first_hit(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  if (ctx->mask_a == 0) {
    return UINT64_MAX;
  }
  return a64_first_bit_from_mask(ctx->mask_a);
}

#if defined(ENABLE_CSSC)
static BENCH_NOINLINE uint64_t cssc_first_hit(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  if (ctx->mask_a == 0) {
    return UINT64_MAX;
  }
  return cssc_ctz_u64(ctx->mask_a);
}
#endif

static BENCH_NOINLINE uint64_t a64_all_hit_positions(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  uint64_t mask = ctx->mask_a;
  size_t count = 0;
  while (mask != 0) {
    uint64_t index = a64_first_bit_from_mask(mask);
    ctx->positions_out[count++] = (uint8_t)index;
    mask &= ~(UINT64_C(1) << index);
  }
  return bench_checksum_bytes(ctx->positions_out, count) ^ count;
}

#if defined(ENABLE_CSSC)
static BENCH_NOINLINE uint64_t cssc_all_hit_positions(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  uint64_t mask = ctx->mask_a;
  size_t count = 0;
  while (mask != 0) {
    uint64_t index = cssc_ctz_u64(mask);
    ctx->positions_out[count++] = (uint8_t)index;
    mask &= ~(UINT64_C(1) << index);
  }
  return bench_checksum_bytes(ctx->positions_out, count) ^ count;
}
#endif

static BENCH_NOINLINE uint64_t a64_selected_data(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  uint64_t mask = ctx->mask_a;
  size_t count = 0;
  while (mask != 0) {
    uint64_t index = a64_first_bit_from_mask(mask);
    ctx->selected_out[count++] = ctx->data[index];
    mask &= ~(UINT64_C(1) << index);
  }
  return bench_checksum_bytes(ctx->selected_out, count) ^ count;
}

#if defined(ENABLE_CSSC)
static BENCH_NOINLINE uint64_t cssc_selected_data(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  uint64_t mask = ctx->mask_a;
  size_t count = 0;
  while (mask != 0) {
    uint64_t index = cssc_ctz_u64(mask);
    ctx->selected_out[count++] = ctx->data[index];
    mask &= ~(UINT64_C(1) << index);
  }
  return bench_checksum_bytes(ctx->selected_out, count) ^ count;
}
#endif

static BENCH_NOINLINE uint64_t a64_predicate_algebra(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  uint64_t combined = (ctx->mask_a & ctx->mask_b) ^ ctx->extra_mask;
  uint64_t first = combined == 0 ? UINT64_MAX : a64_first_bit_from_mask(combined);
  return combined ^ (first << 32U);
}

#if defined(ENABLE_CSSC)
static BENCH_NOINLINE uint64_t cssc_predicate_algebra(void *opaque) {
  arm_scalar_ctx_t *ctx = (arm_scalar_ctx_t *)opaque;
  uint64_t combined = (ctx->mask_a & ctx->mask_b) ^ ctx->extra_mask;
  uint64_t first = combined == 0 ? UINT64_MAX : cssc_ctz_u64(combined);
  return combined ^ (first << 32U);
}
#endif

static void run_suite(const char *suite,
                      arm_scalar_ctx_t *ctx,
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
  uint64_t iters = argc > 1 ? bench_parse_u64(argv[1], 2000000) : 2000000;
  int rounds = argc > 2 ? (int)bench_parse_u64(argv[2], 7) : 7;

  arm_scalar_ctx_t ctx;
  arm_scalar_ctx_init(&ctx);

  const bench_case_t cases[] = {
      {"5.2_neon_to_scalar_mask", neon_to_scalar_mask},
      {"7.1_a64_any_all_count", a64_any_all_count},
      {"7.2_a64_first_hit", a64_first_hit},
      {"7.3_a64_all_hit_positions", a64_all_hit_positions},
      {"7.4_a64_selected_data", a64_selected_data},
      {"7.5_a64_predicate_algebra", a64_predicate_algebra},
#if defined(ENABLE_CSSC)
      {"7.1_cssc_any_all_count", cssc_any_all_count},
      {"7.2_cssc_first_hit", cssc_first_hit},
      {"7.3_cssc_all_hit_positions", cssc_all_hit_positions},
      {"7.4_cssc_selected_data", cssc_selected_data},
      {"7.5_cssc_predicate_algebra", cssc_predicate_algebra},
#endif
  };

  run_suite(
#if defined(ENABLE_CSSC)
      "arm-scalar+cssc",
#else
      "arm-scalar",
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

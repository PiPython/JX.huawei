#include "bench_common.h"

#if !defined(__x86_64__)
#error "bench_x86.c requires x86_64"
#endif

#include <immintrin.h>

enum {
  X86_AVX2_BYTES = 32,
  X86_AVX512_BYTES = 64,
};

typedef struct BENCH_ALIGN(64) x86_ctx {
  uint8_t lhs_a[X86_AVX512_BYTES];
  uint8_t rhs_a[X86_AVX512_BYTES];
  uint8_t lhs_b[X86_AVX512_BYTES];
  uint8_t rhs_b[X86_AVX512_BYTES];
  uint8_t data[X86_AVX512_BYTES];
  uint8_t positions_out[X86_AVX512_BYTES];
  uint8_t selected_out[X86_AVX512_BYTES];
  uint64_t extra_mask;
} x86_ctx_t;

static volatile uint64_t g_sink = 0;

static void x86_ctx_init(x86_ctx_t *ctx) {
  memset(ctx, 0, sizeof(*ctx));
  bench_fill_bytes(ctx->lhs_a, X86_AVX512_BYTES, 0x1111);
  bench_fill_bytes(ctx->rhs_a, X86_AVX512_BYTES, 0x2222);
  bench_fill_bytes(ctx->lhs_b, X86_AVX512_BYTES, 0x3333);
  bench_fill_bytes(ctx->rhs_b, X86_AVX512_BYTES, 0x4444);
  bench_fill_bytes(ctx->data, X86_AVX512_BYTES, 0x5555);

  for (size_t i = 0; i < X86_AVX512_BYTES; ++i) {
    if ((i % 3U) == 0U || (i % 7U) == 1U) {
      ctx->rhs_a[i] = ctx->lhs_a[i];
    } else {
      ctx->rhs_a[i] ^= 0x33U;
    }

    if ((i % 5U) == 0U || (i % 11U) == 2U) {
      ctx->rhs_b[i] = ctx->lhs_b[i];
    } else {
      ctx->rhs_b[i] ^= 0xccU;
    }
  }

  ctx->extra_mask = UINT64_C(0x9249249249249249);
}

static BENCH_NOINLINE uint64_t bench_empty(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  return ctx->extra_mask ^ ctx->lhs_a[0];
}

static BENCH_NOINLINE uint64_t avx2_any_all_count(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m256i lhs = _mm256_loadu_si256((const __m256i *)ctx->lhs_a);
  __m256i rhs = _mm256_loadu_si256((const __m256i *)ctx->rhs_a);
  __m256i cmp = _mm256_cmpeq_epi8(lhs, rhs);
  uint32_t mask = (uint32_t)_mm256_movemask_epi8(cmp);
  uint64_t any = mask != 0U;
  uint64_t all = mask == UINT32_MAX;
  uint64_t count = (uint64_t)__builtin_popcount(mask);
  return any | (all << 1U) | (count << 2U);
}

#if defined(ENABLE_AVX512)
static BENCH_NOINLINE uint64_t avx512_any_all_count(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m512i lhs = _mm512_loadu_si512((const void *)ctx->lhs_a);
  __m512i rhs = _mm512_loadu_si512((const void *)ctx->rhs_a);
  __mmask64 mask = _mm512_cmpeq_epi8_mask(lhs, rhs);
  uint64_t scalar = (uint64_t)mask;
  uint64_t any = scalar != 0U;
  uint64_t all = scalar == UINT64_MAX;
  uint64_t count = (uint64_t)__builtin_popcountll((long long)scalar);
  return any | (all << 1U) | (count << 2U);
}
#endif

static BENCH_NOINLINE uint64_t avx2_first_hit(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m256i lhs = _mm256_loadu_si256((const __m256i *)ctx->lhs_a);
  __m256i rhs = _mm256_loadu_si256((const __m256i *)ctx->rhs_a);
  __m256i cmp = _mm256_cmpeq_epi8(lhs, rhs);
  uint32_t mask = (uint32_t)_mm256_movemask_epi8(cmp);
  if (mask == 0U) {
    return UINT64_MAX;
  }
  return (uint64_t)_tzcnt_u32(mask);
}

#if defined(ENABLE_AVX512)
static BENCH_NOINLINE uint64_t avx512_first_hit(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m512i lhs = _mm512_loadu_si512((const void *)ctx->lhs_a);
  __m512i rhs = _mm512_loadu_si512((const void *)ctx->rhs_a);
  uint64_t mask = (uint64_t)_mm512_cmpeq_epi8_mask(lhs, rhs);
  if (mask == 0U) {
    return UINT64_MAX;
  }
  return (uint64_t)_tzcnt_u64(mask);
}
#endif

static BENCH_NOINLINE uint64_t avx2_all_hit_positions(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m256i lhs = _mm256_loadu_si256((const __m256i *)ctx->lhs_a);
  __m256i rhs = _mm256_loadu_si256((const __m256i *)ctx->rhs_a);
  __m256i cmp = _mm256_cmpeq_epi8(lhs, rhs);
  uint32_t mask = (uint32_t)_mm256_movemask_epi8(cmp);
  size_t count = 0;
  while (mask != 0U) {
    uint32_t index = _tzcnt_u32(mask);
    ctx->positions_out[count++] = (uint8_t)index;
    mask = _blsr_u32(mask);
  }
  return bench_checksum_bytes(ctx->positions_out, count) ^ count;
}

#if defined(ENABLE_AVX512)
static BENCH_NOINLINE uint64_t avx512_all_hit_positions(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m512i lhs = _mm512_loadu_si512((const void *)ctx->lhs_a);
  __m512i rhs = _mm512_loadu_si512((const void *)ctx->rhs_a);
  uint64_t mask = (uint64_t)_mm512_cmpeq_epi8_mask(lhs, rhs);
  size_t count = 0;
  while (mask != 0U) {
    uint64_t index = _tzcnt_u64(mask);
    ctx->positions_out[count++] = (uint8_t)index;
    mask = _blsr_u64(mask);
  }
  return bench_checksum_bytes(ctx->positions_out, count) ^ count;
}
#endif

static BENCH_NOINLINE uint64_t avx2_selected_data(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m256i lhs = _mm256_loadu_si256((const __m256i *)ctx->lhs_a);
  __m256i rhs = _mm256_loadu_si256((const __m256i *)ctx->rhs_a);
  __m256i cmp = _mm256_cmpeq_epi8(lhs, rhs);
  uint32_t mask = (uint32_t)_mm256_movemask_epi8(cmp);
  size_t count = 0;
  while (mask != 0U) {
    uint32_t index = _tzcnt_u32(mask);
    ctx->selected_out[count++] = ctx->data[index];
    mask = _blsr_u32(mask);
  }
  return bench_checksum_bytes(ctx->selected_out, count) ^ count;
}

#if defined(ENABLE_AVX512)
static BENCH_NOINLINE uint64_t avx512_selected_data(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m512i lhs = _mm512_loadu_si512((const void *)ctx->lhs_a);
  __m512i rhs = _mm512_loadu_si512((const void *)ctx->rhs_a);
  __m512i data = _mm512_loadu_si512((const void *)ctx->data);
  __mmask64 mask = _mm512_cmpeq_epi8_mask(lhs, rhs);
  _mm512_mask_compressstoreu_epi8((void *)ctx->selected_out, mask, data);
  uint64_t count = (uint64_t)__builtin_popcountll((long long)(uint64_t)mask);
  return bench_checksum_bytes(ctx->selected_out, (size_t)count) ^ count;
}
#endif

static BENCH_NOINLINE uint64_t avx2_predicate_algebra(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m256i lhs_a = _mm256_loadu_si256((const __m256i *)ctx->lhs_a);
  __m256i rhs_a = _mm256_loadu_si256((const __m256i *)ctx->rhs_a);
  __m256i lhs_b = _mm256_loadu_si256((const __m256i *)ctx->lhs_b);
  __m256i rhs_b = _mm256_loadu_si256((const __m256i *)ctx->rhs_b);
  uint32_t mask_a = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(lhs_a, rhs_a));
  uint32_t mask_b = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(lhs_b, rhs_b));
  uint32_t extra = (uint32_t)ctx->extra_mask;
  uint32_t combined = (mask_a & mask_b) ^ extra;
  return combined;
}

#if defined(ENABLE_AVX512)
static BENCH_NOINLINE uint64_t avx512_predicate_algebra(void *opaque) {
  x86_ctx_t *ctx = (x86_ctx_t *)opaque;
  __m512i lhs_a = _mm512_loadu_si512((const void *)ctx->lhs_a);
  __m512i rhs_a = _mm512_loadu_si512((const void *)ctx->rhs_a);
  __m512i lhs_b = _mm512_loadu_si512((const void *)ctx->lhs_b);
  __m512i rhs_b = _mm512_loadu_si512((const void *)ctx->rhs_b);
  __mmask64 mask_a = _mm512_cmpeq_epi8_mask(lhs_a, rhs_a);
  __mmask64 mask_b = _mm512_cmpeq_epi8_mask(lhs_b, rhs_b);
  __mmask64 combined = _kxor_mask64(_kand_mask64(mask_a, mask_b),
                                    (__mmask64)ctx->extra_mask);
  return (uint64_t)combined;
}
#endif

static void run_suite(const char *suite,
                      x86_ctx_t *ctx,
                      const bench_case_t *cases,
                      size_t count,
                      uint64_t iters,
                      int rounds) {
  bench_print_banner(suite, bench_cycle_frequency());
  uint64_t baseline = bench_measure(bench_empty, ctx, iters, rounds, &g_sink);
  for (size_t i = 0; i < count; ++i) {
    uint64_t cycles = bench_measure(cases[i].fn, ctx, iters, rounds, &g_sink);
    bench_print_result(cases[i].name, cycles, iters, baseline, g_sink);
  }
}

int main(int argc, char **argv) {
  uint64_t iters = argc > 1 ? bench_parse_u64(argv[1], 2000000) : 2000000;
  int rounds = argc > 2 ? (int)bench_parse_u64(argv[2], 7) : 7;

  x86_ctx_t ctx;
  x86_ctx_init(&ctx);

  const bench_case_t cases[] = {
      {"7.1_avx2_any_all_count", avx2_any_all_count},
      {"7.2_avx2_first_hit", avx2_first_hit},
      {"7.3_avx2_all_hit_positions", avx2_all_hit_positions},
      {"7.4_avx2_selected_data", avx2_selected_data},
      {"7.5_avx2_predicate_algebra", avx2_predicate_algebra},
#if defined(ENABLE_AVX512)
      {"7.1_avx512_any_all_count", avx512_any_all_count},
      {"7.2_avx512_first_hit", avx512_first_hit},
      {"7.3_avx512_all_hit_positions", avx512_all_hit_positions},
      {"7.4_avx512_selected_data", avx512_selected_data},
      {"7.5_avx512_predicate_algebra", avx512_predicate_algebra},
#endif
  };

  run_suite(
#if defined(ENABLE_AVX512)
      "x86-avx2+avx512",
#else
      "x86-avx2",
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

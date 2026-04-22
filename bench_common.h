#ifndef TO_BITMASK_BENCH_COMMON_H
#define TO_BITMASK_BENCH_COMMON_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

#define BENCH_ALIGN(N) __attribute__((aligned(N)))
#define BENCH_NOINLINE __attribute__((noinline))

typedef uint64_t (*bench_fn_t)(void *);

typedef struct bench_case {
  const char *name;
  bench_fn_t fn;
} bench_case_t;

static inline uint64_t bench_mix_u64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

static inline void bench_fill_bytes(uint8_t *dst, size_t n, uint64_t seed) {
  uint64_t x = seed;
  for (size_t i = 0; i < n; ++i) {
    x = bench_mix_u64(x + 0x9e3779b97f4a7c15ULL + i);
    dst[i] = (uint8_t)(x & 0xffU);
  }
}

static inline uint64_t bench_scalar_mask_from_bytes(const uint8_t *lhs,
                                                    const uint8_t *rhs,
                                                    size_t n) {
  uint64_t mask = 0;
  size_t limit = n > 64 ? 64 : n;
  for (size_t i = 0; i < limit; ++i) {
    if (lhs[i] == rhs[i]) {
      mask |= (UINT64_C(1) << i);
    }
  }
  return mask;
}

static inline uint64_t bench_mask_all(size_t n) {
  if (n >= 64) {
    return UINT64_MAX;
  }
  return (UINT64_C(1) << n) - 1;
}

static inline uint64_t bench_checksum_bytes(const uint8_t *data, size_t n) {
  uint64_t acc = 0x123456789abcdef0ULL;
  for (size_t i = 0; i < n; ++i) {
    acc = bench_mix_u64(acc ^ ((uint64_t)data[i] << ((i & 7U) * 8U)));
  }
  return acc ^ n;
}

static inline uint64_t bench_popcount_loop_u64(uint64_t x) {
  uint64_t count = 0;
  while (x != 0) {
    count += (x & 1U);
    x >>= 1U;
  }
  return count;
}

#if defined(__aarch64__)
static inline uint64_t bench_rdcycle(void) {
  uint64_t value;
  __asm__ volatile(
      "isb\n\t"
      "mrs %0, cntvct_el0\n\t"
      : "=r"(value));
  return value;
}

static inline uint64_t bench_cycle_frequency(void) {
  uint64_t value;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
  return value;
}

static inline const char *bench_counter_source(void) {
#if defined(__APPLE__)
  return "cntvct_el0 (generic timer ticks; PMCCNTR_EL0 unavailable in userspace)";
#else
  return "cntvct_el0";
#endif
}
#elif defined(__x86_64__)
static inline uint64_t bench_rdcycle(void) {
  unsigned aux = 0;
  _mm_lfence();
  uint64_t value = __rdtscp(&aux);
  _mm_lfence();
  return value;
}

static inline uint64_t bench_cycle_frequency(void) {
  return 0;
}

static inline const char *bench_counter_source(void) {
  return "rdtscp";
}
#else
#error "Unsupported architecture for cycle benchmark"
#endif

static inline uint64_t bench_measure(bench_fn_t fn,
                                     void *ctx,
                                     uint64_t iters,
                                     int rounds,
                                     volatile uint64_t *sink) {
  uint64_t best = UINT64_MAX;
  uint64_t local_sink = 0;

  for (int round = 0; round < rounds; ++round) {
    for (size_t warm = 0; warm < 256; ++warm) {
      local_sink ^= fn(ctx);
    }

    uint64_t start = bench_rdcycle();
    for (uint64_t iter = 0; iter < iters; ++iter) {
      local_sink ^= fn(ctx);
    }
    uint64_t end = bench_rdcycle();

    uint64_t delta = end - start;
    if (delta < best) {
      best = delta;
    }
  }

  *sink ^= local_sink;
  return best;
}

static inline void bench_print_banner(const char *suite, uint64_t freq) {
  printf("# suite=%s\n", suite);
  printf("# counter_source=%s\n", bench_counter_source());
  if (freq != 0) {
    printf("# cycle_counter_hz=%" PRIu64 "\n", freq);
  }
  printf("%-32s %14s %14s %18s\n",
         "kernel",
         "cycles/call",
         "iters",
         "sink");
}

static inline void bench_print_result(const char *name,
                                      uint64_t cycles,
                                      uint64_t iters,
                                      uint64_t baseline_cycles,
                                      uint64_t sink) {
  uint64_t adjusted = cycles > baseline_cycles ? cycles - baseline_cycles : 0;
  double cycles_per_call =
      iters == 0 ? 0.0 : (double)adjusted / (double)iters;
  printf("%-32s %14.2f %14" PRIu64 " %18" PRIu64 "\n",
         name,
         cycles_per_call,
         iters,
         sink);
}

static inline uint64_t bench_parse_u64(const char *text, uint64_t fallback) {
  if (text == NULL || *text == '\0') {
    return fallback;
  }
  char *end = NULL;
  unsigned long long value = strtoull(text, &end, 10);
  if (end == text || *end != '\0') {
    return fallback;
  }
  return (uint64_t)value;
}

#endif

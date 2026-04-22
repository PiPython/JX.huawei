#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
CC_ARM=${CC_ARM:-clang}
CC_SVE=${CC_SVE:-${AARCH64_LINUX_CC:-clang}}
CC_X86=${CC_X86:-clang}
MODE=${1:-native}

mkdir -p "$BUILD_DIR"

host_arch=$(uname -m)
host_os=$(uname -s)

build_arm_base() {
  "$CC_ARM" -O3 -std=c11 -Wall -Wextra -pedantic \
    "$ROOT_DIR/bench_arm_scalar.c" \
    -o "$BUILD_DIR/bench_arm_scalar_base"
}

build_arm_cssc() {
  "$CC_ARM" -O3 -std=c11 -Wall -Wextra -pedantic -DENABLE_CSSC \
    -march=armv8.9-a+cssc \
    "$ROOT_DIR/bench_arm_scalar.c" \
    -o "$BUILD_DIR/bench_arm_scalar_cssc"
}

build_arm_sve() {
  "$CC_SVE" -O3 -std=c11 -Wall -Wextra -pedantic \
    -march=armv9-a+sve \
    "$ROOT_DIR/bench_arm_sve.c" \
    -o "$BUILD_DIR/bench_arm_sve"
}

build_arm_sve2p1() {
  "$CC_SVE" -O3 -std=c11 -Wall -Wextra -pedantic -DENABLE_SVE2P1 \
    -march=armv9-a+sve2p1 \
    "$ROOT_DIR/bench_arm_sve.c" \
    -o "$BUILD_DIR/bench_arm_sve2p1"
}

build_arm_sve2p2() {
  "$CC_SVE" -O3 -std=c11 -Wall -Wextra -pedantic -DENABLE_SVE2P1 -DENABLE_SVE2P2 \
    -march=armv9-a+sve2p2 \
    "$ROOT_DIR/bench_arm_sve.c" \
    -o "$BUILD_DIR/bench_arm_sve2p2"
}

build_x86_avx2() {
  "$CC_X86" -O3 -std=c11 -Wall -Wextra -pedantic \
    -mavx2 -mbmi -mpopcnt \
    "$ROOT_DIR/bench_x86.c" \
    -o "$BUILD_DIR/bench_x86_avx2"
}

build_x86_avx512() {
  "$CC_X86" -O3 -std=c11 -Wall -Wextra -pedantic -DENABLE_AVX512 \
    -mavx512f -mavx512bw -mavx512vbmi2 -mbmi -mpopcnt \
    "$ROOT_DIR/bench_x86.c" \
    -o "$BUILD_DIR/bench_x86_avx512"
}

usage() {
  cat <<EOF
Usage: $0 [native|all|arm-base|arm-cssc|arm-sve|arm-sve2p1|arm-sve2p2|x86-avx2|x86-avx512]

Environment:
  CC_ARM   compiler for A64/NEON/CSSC benchmarks
  CC_SVE   compiler for SVE/SVE2p1/SVE2p2 benchmarks
  CC_X86   compiler for x86 benchmarks

Notes:
  - native: builds what is directly runnable on the current host.
  - SVE and x86 binaries usually need to be built on matching hardware/toolchains.
EOF
}

case "$MODE" in
  native)
    case "$host_arch" in
      arm64|aarch64)
        build_arm_base
        ;;
      x86_64)
        build_x86_avx2
        ;;
      *)
        echo "unsupported native host: $host_arch" >&2
        exit 1
        ;;
    esac
    ;;
  all)
    build_arm_base
    build_arm_cssc
    build_arm_sve
    build_arm_sve2p1
    build_arm_sve2p2
    build_x86_avx2
    build_x86_avx512
    ;;
  arm-base)
    build_arm_base
    ;;
  arm-cssc)
    build_arm_cssc
    ;;
  arm-sve)
    build_arm_sve
    ;;
  arm-sve2p1)
    build_arm_sve2p1
    ;;
  arm-sve2p2)
    build_arm_sve2p2
    ;;
  x86-avx2)
    build_x86_avx2
    ;;
  x86-avx512)
    build_x86_avx512
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    usage >&2
    exit 1
    ;;
esac

printf 'built mode=%s in %s on %s/%s\n' "$MODE" "$BUILD_DIR" "$host_os" "$host_arch"

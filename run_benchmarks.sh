#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
MODE=${1:-native}
ITERS=${2:-}
ROUNDS=${3:-}

host_arch=$(uname -m)

resolve_binary() {
  case "$1" in
    native)
      case "$host_arch" in
        arm64|aarch64) printf '%s\n' "$BUILD_DIR/bench_arm_scalar_base" ;;
        x86_64) printf '%s\n' "$BUILD_DIR/bench_x86_avx2" ;;
        *) return 1 ;;
      esac
      ;;
    arm-base) printf '%s\n' "$BUILD_DIR/bench_arm_scalar_base" ;;
    arm-cssc) printf '%s\n' "$BUILD_DIR/bench_arm_scalar_cssc" ;;
    arm-sve) printf '%s\n' "$BUILD_DIR/bench_arm_sve" ;;
    arm-sve2p1) printf '%s\n' "$BUILD_DIR/bench_arm_sve2p1" ;;
    arm-sve2p2) printf '%s\n' "$BUILD_DIR/bench_arm_sve2p2" ;;
    x86-avx2) printf '%s\n' "$BUILD_DIR/bench_x86_avx2" ;;
    x86-avx512) printf '%s\n' "$BUILD_DIR/bench_x86_avx512" ;;
    *) return 1 ;;
  esac
}

usage() {
  cat <<EOF
Usage: $0 [native|arm-base|arm-cssc|arm-sve|arm-sve2p1|arm-sve2p2|x86-avx2|x86-avx512] [iters] [rounds]

Examples:
  $0 native
  $0 arm-base 2000000 7
  $0 x86-avx512 1000000 9
EOF
}

case "$MODE" in
  -h|--help|help)
    usage
    exit 0
    ;;
esac

"$ROOT_DIR/build_benchmarks.sh" "$MODE"
binary=$(resolve_binary "$MODE")

if [ ! -x "$binary" ]; then
  echo "benchmark binary not found: $binary" >&2
  exit 1
fi

set -- "$binary"
if [ -n "$ITERS" ]; then
  set -- "$@" "$ITERS"
fi
if [ -n "$ROUNDS" ]; then
  set -- "$@" "$ROUNDS"
fi

exec "$@"

#!/usr/bin/env bash
# build_templates.sh — Build all C/C++ templates for a given board arch.
# Parameterized: no hardcoded paths or IPs.
#
# Usage:
#   ./build_templates.sh --arch aarch64 --sdk /path/to/rk-sdk
#   ./build_templates.sh --arch armhf  --sdk /path/to/rv1106-sdk  # RV1106
#   ./build_templates.sh --arch aarch64 --sdk /path/to/sdk --template cpp-mpp
#
# Requires: cross compiler (aarch64-linux-gnu-gcc or arm-linux-gnueabihf-gcc)
#           in PATH, and RK SDK sysroot with include/ and lib/.
set -euo pipefail

ARCH="aarch64"
SDK=""
TEMPLATE=""
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TPL_DIR="$SCRIPT_DIR/../templates"
OUT_DIR="$SCRIPT_DIR/../build-out"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) ARCH="$2"; shift 2 ;;
        --sdk)  SDK="$2"; shift 2 ;;
        --template) TEMPLATE="$2"; shift 2 ;;
        --out)  OUT_DIR="$2"; shift 2 ;;
        *) echo "unknown arg: $1"; exit 1 ;;
    esac
done

if [[ -z "$SDK" ]]; then echo "ERROR: --sdk required"; exit 1; fi

# Select cross compiler
case "$ARCH" in
    aarch64) CC=aarch64-linux-gnu-gcc; CXX=aarch64-linux-gnu-g++; LIBDIR=lib/aarch64 ;;
    armhf)   CC=arm-linux-gnueabihf-gcc; CXX=arm-linux-gnueabihf-g++; LIBDIR=lib/armhf ;;
    *) echo "unsupported arch: $ARCH (use aarch64|armhf)"; exit 1 ;;
esac

command -v "$CC" >/dev/null || { echo "missing $CC"; exit 1; }

mkdir -p "$OUT_DIR"
RK_CFLAGS="-I$SDK/include -L$SDK/$LIBDIR"
RK_LIBS_MPP="-lrockchip_mpp -lrockchip_mpp_v2"
RK_LIBS_RGA="-lrga"
RK_LIBS_RKNN="-lrknnrt"
[[ "$ARCH" == "armhf" ]] && RK_LIBS_RKNN="-lrknnmrt"  # RV1106 micro runtime

build_one() {
    local name="$1"
    local src="$TPL_DIR/$name"
    local out="$OUT_DIR/$name"
    echo "=== build $name ==="
    case "$name" in
        c-static)
            "$CC" -static -O2 "$src/main.c" -o "$out" ;;
        c-rknn)
            "$CC" -O2 "$src/main.c" $RK_CFLAGS $RK_LIBS_RKNN -o "$out" ;;
        cpp-mpp)
            "$CXX" -O2 "$src/main.cpp" $RK_CFLAGS $RK_LIBS_MPP -o "$out" ;;
        cpp-mpp-rknn-rga)
            "$CXX" -O2 "$src/main.cpp" $RK_CFLAGS $RK_LIBS_MPP $RK_LIBS_RGA $RK_LIBS_RKNN -o "$out" ;;
        cpp-zero-copy)
            "$CXX" -O2 "$src/main.cpp" $RK_CFLAGS $RK_LIBS_MPP $RK_LIBS_RGA $RK_LIBS_RKNN -o "$out" ;;
        cmake-sdk)
            echo "skip cmake-sdk (use cmake directly)"; return ;;
        go-cgo-static|rust-musl)
            echo "skip $name (use go/cross build directly)"; return ;;
        *) echo "unknown template: $name"; return ;;
    esac
    echo "  -> $out"
}

if [[ -n "$TEMPLATE" ]]; then
    build_one "$TEMPLATE"
else
    for d in "$TPL_DIR"/*/; do
        build_one "$(basename "$d")"
    done
fi

echo "done. outputs in $OUT_DIR"
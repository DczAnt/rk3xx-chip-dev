#!/usr/bin/env bash
# docker/build.sh — Build all Docker images for a given arch + SDK.
# Parameterized: no hardcoded SDK paths.
#
# Usage:
#   ./build.sh --arch aarch64 --sdk-tarball /path/to/sdk.tar.gz
#   ./build.sh --arch armhf --sdk-tarball /path/to/rv1106-sdk.tar.gz
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ARCH="aarch64"
SDK_TAR=""
APP_SRC=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) ARCH="$2"; shift 2 ;;
        --sdk-tarball) SDK_TAR="$2"; shift 2 ;;
        --app-src) APP_SRC="$2"; shift 2 ;;
        *) echo "unknown: $1"; exit 1 ;;
    esac
done

[[ -n "$SDK_TAR" ]] || { echo "ERROR: --sdk-tarball required"; exit 1; }
[[ -f "$SDK_TAR" ]] || { echo "ERROR: $SDK_TAR not found"; exit 1; }

case "$ARCH" in
    aarch64) PREFIX=aarch64-linux-gnu ;;
    armhf)   PREFIX=arm-linux-gnueabihf ;;
    *) echo "unsupported arch: $ARCH"; exit 1 ;;
esac

SDK_NAME="$(basename "$SDK_TAR")"
CTX="$(dirname "$SDK_TAR")"

echo "=== 1/3 base ==="
docker build -f "$SCRIPT_DIR/Dockerfile.base" -t rk-base:${ARCH} \
    --build-arg ARCH=${ARCH} --build-arg GCC_PREFIX=${PREFIX} "$SCRIPT_DIR"

echo "=== 2/3 sdk ==="
docker build -f "$SCRIPT_DIR/Dockerfile.sdk" -t rk-sdk:${ARCH} \
    --build-arg ARCH=${ARCH} --build-arg GCC_PREFIX=${PREFIX} \
    --build-arg SDK_TARBALL=${SDK_NAME} "$CTX"

if [[ -n "$APP_SRC" ]]; then
    echo "=== 3/3 final (app=$APP_SRC) ==="
    docker build -f "$SCRIPT_DIR/Dockerfile.final" -t rk-app:${ARCH} \
        --build-arg ARCH=${ARCH} --build-arg GCC_PREFIX=${PREFIX} \
        --build-arg SDK_TARBALL=${SDK_NAME} \
        --build-arg APP_SRC=${APP_SRC} "$CTX"
fi

echo "done. images: rk-base:${ARCH} rk-sdk:${ARCH}${APP_SRC:+ rk-app:${ARCH}}"
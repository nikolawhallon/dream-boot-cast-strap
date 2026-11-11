#!/usr/bin/env bash
# Builds the Simulant engine from source, for one target and build type:
#
#   dc-build-simulant.sh linux     [Release|Debug]  -> sdk/build/simulant-linux-<type>/simulant/libsimulant.so
#   dc-build-simulant.sh dreamcast [Release|Debug]  -> sdk/build/simulant-dreamcast-<type>/simulant/libsimulant.a
#
# The source is a pinned checkout in sdk/simulant (cloned on first use). On Dreamcast the library
# also contains GLdc (OpenGL 1.x for the PowerVR2 GPU) and ALdc (OpenAL for the AICA sound chip).
# Games link the build tree directly, because Simulant's own `make install` is broken upstream.
# Project Makefiles call this automatically when the library they need is missing.
set -euo pipefail

if [ -z "${IN_NIX_SHELL:-}" ]; then
  echo "Run this from inside nix-shell." >&2
  exit 1
fi

TARGET="${1:?usage: $0 linux|dreamcast [Release|Debug]}"
BUILD_TYPE="${2:-Release}"

KIT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$KIT/sdk"
SRC="$SDK/simulant"
SIMULANT_REV="4a5f5799d0232c1a509b186262a214da0f138894"   # Simulant main, 2026-06-17

if [ ! -d "$SRC/.git" ]; then
  git clone https://gitlab.com/simulant/simulant.git "$SRC"
fi
if [ "$(git -C "$SRC" rev-parse HEAD)" != "$SIMULANT_REV" ]; then
  git -C "$SRC" fetch -q origin
  git -C "$SRC" checkout -q "$SIMULANT_REV"
fi
git -C "$SRC" submodule update --init --recursive

BUILD="$SDK/build/simulant-$TARGET-${BUILD_TYPE,,}"

CMAKE_ARGS=(
  -S "$SRC" -B "$BUILD" -G Ninja
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DSIMULANT_BUILD_TESTS=OFF
  -DSIMULANT_BUILD_SAMPLES=OFF
)

case "$TARGET" in
  linux) ;;
  dreamcast)
    if [ -z "${KOS_BASE:-}" ]; then
      echo "KOS environment not loaded - run dc-bootstrap.sh, then re-enter nix-shell." >&2
      exit 1
    fi
    CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$SRC/toolchains/Dreamcast.cmake")
    ;;
  *) echo "unknown target: $TARGET" >&2; exit 1 ;;
esac

cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD" -j"$(nproc)"
echo "Simulant ($TARGET, $BUILD_TYPE) built in $BUILD"

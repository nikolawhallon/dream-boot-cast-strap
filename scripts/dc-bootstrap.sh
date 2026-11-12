#!/usr/bin/env bash
# One-time build of the Dreamcast SDK into <kit>/sdk. Run inside the kit's nix-shell.
#
#   sdk/toolchains/sh-elf   SH-4 cross compiler: gcc 13.2, binutils 2.43, newlib 4.3 (KOS dc-chain)
#   sdk/kos                 KallistiOS v2.2.1 + patches/, release libs + debug libs (lib/dreamcast/debug)
#
# (Simulant itself is built separately, per target, by dc-build-simulant.sh.)
#
# Re-running is safe: finished steps are skipped. Each step leaves a marker in sdk/.done/;
# delete a marker to redo that step, or delete sdk/ entirely to start over.
set -euo pipefail

if [ -z "${IN_NIX_SHELL:-}" ]; then
  echo "Run this from inside nix-shell." >&2
  exit 1
fi

KIT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$KIT/sdk"
DONE="$SDK/.done"
JOBS="$(nproc)"

# KallistiOS revision: the one Simulant's own Dreamcast SDK image uses
# (sdk/simulant/platforms/dreamcast/Dockerfile).
KOS_REV="b125c930da1a7969c5b9760da98254c666b3eeba"   # v2.2.1

# Floating-point ABI. Must match what Simulant compiles with (toolchains/Dreamcast.cmake uses
# -m4-single-only). KOS 2.2 defaults to -m4-single, which passes float arguments in different
# registers: mixing the two scrambles every float handed from GLdc to KOS (garbled colors, no geometry).
KOS_PRECISION="-m4-single-only"

mkdir -p "$DONE" "$SDK/bin" "$SDK/toolchains"

step() { [ -f "$DONE/$1" ]; }
mark() { touch "$DONE/$1"; }
log()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

# ---------------------------------------------------------------- 1. KallistiOS source
if ! step sources; then
  log "Fetching KallistiOS"
  [ -d "$SDK/kos/.git" ] || git clone https://github.com/KallistiOS/KallistiOS.git "$SDK/kos"
  git -C "$SDK/kos" fetch -q origin
  git -C "$SDK/kos" checkout -q "$KOS_REV"
  mark sources
fi

# ---------------------------------------------------------------- 2. KOS patches
# Fixes backported from newer KOS (see patches/). The step is keyed to the patches' contents,
# so adding or changing a patch re-runs it on the next bootstrap: the KOS tree is reset to
# KOS_REV, every patch applied, and KOS rebuilt.
PATCHES_ID="patches-$(cat "$KIT"/patches/kos-*.patch | sha1sum | cut -c1-12)"
if ! step "$PATCHES_ID"; then
  git -C "$SDK/kos" checkout -q -- .
  for p in "$KIT"/patches/kos-*.patch; do
    log "Applying $(basename "$p")"
    git -C "$SDK/kos" apply "$p"
  done
  rm -f "$DONE"/kos-* "$DONE"/patches*
  mark "$PATCHES_ID"
fi

# ---------------------------------------------------------------- 3. environ.sh
# KOS is configured entirely through environment variables, set by sourcing environ.sh.
# Generated from KOS's sample with our paths. shell.nix sources it on entry.
log "Writing kos/environ.sh"
sed \
  -e "s|^export KOS_BASE=.*|export KOS_BASE=\"$SDK/kos\"|" \
  -e "s|^export KOS_CC_BASE=.*|export KOS_CC_BASE=\"$SDK/toolchains/sh-elf\"|" \
  -e "s|^export DC_ARM_BASE=.*|export DC_ARM_BASE=\"$SDK/toolchains/arm-eabi\"|" \
  -e "s|^export DC_TOOLS_BASE=.*|export DC_TOOLS_BASE=\"$SDK/bin\"|" \
  -e "s|^export KOS_SH4_PRECISION=.*|export KOS_SH4_PRECISION=\"$KOS_PRECISION\"|" \
  "$SDK/kos/doc/environ.sh.sample" > "$SDK/kos/environ.sh"
# Debug flavour, used only to build KOS's debug libs
sed '$ i\export KOS_CFLAGS="${KOS_CFLAGS} -DKOS_DEBUG=1"' "$SDK/kos/environ.sh" > "$SDK/kos/environ-debug.sh"

# ---------------------------------------------------------------- 4. SH-4 cross toolchain
if ! step toolchain; then
  log "Building the sh-elf toolchain with dc-chain (20-40 minutes)"
  cd "$SDK/kos/utils/dc-chain"
  cp Makefile.default.cfg Makefile.cfg
  sed -i \
    -e "s|^sh_toolchain_path=.*|sh_toolchain_path=$SDK/toolchains/sh-elf|" \
    -e "s|^arm_toolchain_path=.*|arm_toolchain_path=$SDK/toolchains/arm-eabi|" \
    -e "s|^makejobs=.*|makejobs=$JOBS|" \
    -e "s|^enable_objc=.*|enable_objc=0|" \
    -e "s|^enable_objcpp=.*|enable_objcpp=0|" \
    Makefile.cfg
  # - SH-4 toolchain only: the ARM (sound CPU) toolchain isn't needed because KOS ships a
  #   prebuilt sound driver. No gdb either.
  # - NixOS has no /bin/bash, which dc-chain hardcodes.
  # - Host compiler is GCC 13 (see shell.nix).
  CC="$DC_HOST_CC" CXX="$DC_HOST_CXX" make build-sh4 SHELL="$(command -v bash)"
  # Drop the ~2GB of downloaded/unpacked sources
  rm -rf binutils-* gcc-* newlib-* logs config.guess config.sub
  mark toolchain
fi

# ---------------------------------------------------------------- 5. KallistiOS
set +u   # environ.sh references unset variables
if ! step "kos$KOS_PRECISION"; then
  log "Building KallistiOS (debug libs)"
  ( . "$SDK/kos/environ-debug.sh"; cd "$SDK/kos" && make clean >/dev/null && make -j"$JOBS" )
  mkdir -p "$SDK/kos/lib/dreamcast/debug"
  mv "$SDK/kos/lib/dreamcast/"*.a "$SDK/kos/lib/dreamcast/debug/"

  log "Building KallistiOS (release libs)"
  ( . "$SDK/kos/environ.sh"; cd "$SDK/kos" && make clean >/dev/null && make -j"$JOBS" )
  mark "kos$KOS_PRECISION"
fi

log "Dreamcast SDK ready. Leave and re-enter nix-shell to load it."

# Dreamcast + Linux dev shell for Simulant games. Projects reuse it from their own shell.nix:
#
#   import /path/to/this/kit/shell.nix { }
#
# What you get inside the shell:
#   - native toolchain + SDL2/OpenAL/GL for the Linux build
#   - the Dreamcast SDK in $DC_SDK (sh-elf-gcc, KallistiOS env vars), once it has been built
#     with `dc-bootstrap.sh`
#   - mkdcdisc (ELF -> bootable disc image), Flycast (emulator), headless screenshot tools
#   - the kit's scripts on PATH: dc-bootstrap.sh, dc-build-simulant.sh, dc-new-project.sh, dc-screenshot.sh
#
# nixpkgs is pinned so the shell is reproducible. Bump `nixpkgsRev` (and the hash) to update.
let
  nixpkgsRev = "6fcc160353f63555b560f25588925133c8f61f2a";
  pinned = builtins.fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/${nixpkgsRev}.tar.gz";
    sha256 = "0rmvi7fa3brb2zvj8y3dzl819q33xjrw0j95wwxqhb099kwhp3y4";
  };
in
{ pkgs ? import pinned { } }:

let
  kit = toString ./.;

  # The native (Linux) build uses nixpkgs' default GCC, matching the libstdc++ that
  # SDL2/OpenAL were built against. The Dreamcast cross-compiler (GCC 13.2 sources) is
  # bootstrapped with host GCC 13, which builds those older sources more reliably.
  hostCC13 = pkgs.gcc13;

  # Flycast from upstream master: with v2.7, sustained two-way traffic over the emulated
  # Broadband Adapter (streaming microphone audio up, speech down) hung the emulated network.
  # Master has several BBA fixes since the release -- c0fb036d (stalled uploads in the picoTCP
  # proxy), 38a3d83c (frame handover races), 5543a95c (polling) -- and works. Go back to plain
  # pkgs.flycast once a release includes them.
  flycast = pkgs.flycast.overrideAttrs (old: {
    version = "2.7-unstable-2026-11-08";
    src = pkgs.fetchFromGitHub {
      owner = "flyinghead";
      repo = "flycast";
      rev = "222c9530da7552fc1a95f33acfb4848856fafa28";
      hash = "sha256-i6/stZ2ZyqThCtq7SgNoq+ArQaDGaIwIGeBa/1Bnq3k=";
      fetchSubmodules = true;
    };
  });

  # ELF -> self-booting Dreamcast disc image (.cdi/.gdi), by the Simulant authors.
  mkdcdisc = pkgs.stdenv.mkDerivation {
    pname = "mkdcdisc";
    version = "0.0.4-unstable-2026-11-08";
    src = pkgs.fetchFromGitLab {
      owner = "simulant";
      repo = "mkdcdisc";
      rev = "d158f773bdcd5972f79d1e5837ed3797cd0b578e";
      sha256 = "0cs4cj1xrxwxvzkwbkcxf3kmyz1bnpfcj0wqcyncvnd8c7ywfvfd";
    };
    nativeBuildInputs = with pkgs; [ meson ninja pkg-config ];
    buildInputs = with pkgs; [ libisofs libpng zlib ];
    installPhase = ''
      runHook preInstall
      install -Dm755 mkdcdisc $out/bin/mkdcdisc
      runHook postInstall
    '';
  };
in
pkgs.mkShell {
  name = "dreamcast-simulant";

  DC_HOST_CC = "${hostCC13}/bin/gcc";
  DC_HOST_CXX = "${hostCC13}/bin/g++";

  nativeBuildInputs = with pkgs; [
    # Build tooling (game + Simulant use CMake/Ninja; mkdcdisc uses meson)
    cmake
    gnumake
    ninja
    meson
    pkg-config
    git
    python3
    which
    patch
    diffutils
    file

    # Building the sh-elf cross toolchain (KOS dc-chain)
    texinfo
    bison
    flex
    curl
    gnutar
    xz
    bzip2
    gzip
    unzip
    gawk
    gnused

    # Disc image builder + emulator
    mkdcdisc
    flycast   # upstream master, see above

    # Headless screenshots (dc-screenshot.sh)
    xvfb-run
    xwd
    imagemagick
  ];

  buildInputs = with pkgs; [
    # Toolchain build host libs
    gmp
    mpfr
    libmpc
    isl
    zlib
    # KOS (libelf) and its image tools (vqenc/kmgenc use libpng/libjpeg)
    elfutils
    libpng
    libjpeg

    # Simulant native (Linux) build
    SDL2
    openal
    libGL
    libGLU
  ];

  # Nix's default hardening flags (fortify, PIE, ...) break parts of the GCC/newlib
  # cross build. They don't apply to the resulting sh-elf compiler anyway.
  hardeningDisable = [ "all" ];

  shellHook = ''
    export DC_KIT="${kit}"
    export DC_SDK="$DC_KIT/sdk"
    export PATH="$DC_KIT/scripts:$PATH"

    # Flycast & SDL need the system GL drivers on NixOS
    export LD_LIBRARY_PATH="/run/opengl-driver/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

    if [ -x "$DC_SDK/toolchains/sh-elf/bin/sh-elf-gcc" ] && [ -f "$DC_SDK/kos/environ.sh" ]; then
      . "$DC_SDK/kos/environ.sh"
      echo "Dreamcast SDK loaded (KOS_BASE=$KOS_BASE)"
    else
      echo "Dreamcast SDK not built yet. Run: dc-bootstrap.sh"
    fi
  '';
}

# Notes

Reference material. Start with [README.md](README.md).

## How the pieces fit

```
your game (sources/*.cpp), compiled twice
   │
dbcs (lib/dbcs)        Linux: Simulant layers      │ Dreamcast: straight to the PowerVR
Simulant engine        SDL2, OpenGL (gl1x), OpenAL │ KallistiOS, GLdc, ALdc (inside libsimulant.a)
compiler               g++ (nixpkgs)               │ sh-elf-gcc 13.2 → game.elf → mkdcdisc → game.cdi
```

- **`shell.nix`**: every host tool, pinned. Sets `DC_KIT`, `DC_SDK`, puts `scripts/` on `PATH`,
  sources `sdk/kos/environ.sh` once it exists.
- **`sdk/toolchains/sh-elf`**: SH-4 cross compiler, built by KallistiOS's `dc-chain`.
- **`sdk/kos`**: KallistiOS, the Dreamcast SDK/OS (threads, `/cd` filesystem, controllers,
  video, sound, network). Configured by environment variables in `environ.sh`.
- **GLdc / ALdc**: OpenGL 1.x and OpenAL for the Dreamcast, compiled into `libsimulant.a`.
- **Simulant** (`sdk/simulant`, built into `sdk/build/simulant-<target>-<type>`): the engine.
  The template forces its fixed-function `gl1x` renderer on PC to match the Dreamcast.
- **mkdcdisc**: `game.elf` → scrambled `1ST_READ.BIN` + `IP.BIN` + `assets/` → bootable `.cdi`.
- **Flycast**: emulator; its built-in BIOS replacement needs no BIOS dump.

## What things cost (Flycast, Dreamcast build)

Relative numbers are reliable; absolute ones still need checking on hardware.

| | per frame |
|---|---|
| Simulant draw call (per actor/submesh, per layer) | ~35 µs |
| Simulant UI widgets | several draw calls each (4 HUDs × 44 widgets ≈ 30 ms) |
| GLdc, per triangle | ~2.2 µs |
| dbcs, per vertex | ~94 cycles (~2M vertices/s) |
| One ~50-character `printf` (serial) | ~4 ms |

- Draw calls are expensive: merge small things, use one atlas per HUD.
- Split screen multiplies the scene cost by the number of views.
- The PowerVR rasterises in hardware but doesn't transform: projection, clipping and culling
  run on the SH-4.

## Measuring

- `make shot-dc` saves the serial log next to the screenshot. `dc-screenshot.sh dc out.png 30`
  takes it 30 s in, with its own log.
- Look for repeated warnings: `sort build/screenshot-dc.log | uniq -c | sort -rn | head`.
  KallistiOS reports misuse there and carries on, and each line costs milliseconds.
- `timer_us_gettime64()` (µs), `app->stats` (frame rate), `app->ram_usage_in_bytes()`,
  `pvr_get_stats()`. dbcs: `renderer->triangles()`, `draw_us()`, `overlay_us()`, `overlay_quads()`.
- A bot mode (no controller needed) lets headless runs measure real gameplay. Make bots play
  like people (fire is a press, not held).

## How dbcs draws on the Dreamcast

Simulant still runs its frame through GLdc; dbcs adds its polygons to that frame.

- **Hook**: link with `--wrap=pvr_scene_begin --wrap=pvr_list_begin --wrap=pvr_scene_finish`
  (`dbcs.cmake` does). Wrap KallistiOS functions, not GLdc's: Simulant is built with LTO, so
  GLdc's internal calls never reach the linker.
- **Open each PowerVR list at most once per frame.** A second open fails and prints two
  warnings per frame (~8.5 ms). dbcs draws into GLdc's lists and opens the ones GLdc skips.
- **Textures**: loaded by Simulant, uploaded by GLdc; `_glGetBoundTexture()` gives the address
  and format. Uploads are lazy, so headers are built on first draw.
- **Per triangle**: `mat_apply` transform, reject off-view, clip only near-plane/viewport
  crossings, cull back faces on the CPU, write through store queues (`pvr_dr_*`). Duplicate
  vertices are merged and quads sent as 4-vertex strips.
- GLdc's GL types are macros (`#define GLsizei unsigned int`): use `static_cast`, not `GLsizei(x)`.

Details in `lib/dbcs/renderer_dc.cpp`.

## Networking and the microphone

KallistiOS has TCP/IP with BSD sockets, Broadband/LAN Adapter drivers, and a microphone driver.
All work in Flycast. Dream Wars uses all of it.

- **Turn networking on** in `main.cpp`:
  ```cpp
  #ifdef __DREAMCAST__
  #include <kos/init.h>
  KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NET);   // DHCP
  #endif
  ```
  Then plain `socket()`/`connect()`/`send()`/`recv()` and `std::thread`.
- **No TLS.** Talk plain HTTP/WebSocket to a relay, e.g.
  `socat TCP-LISTEN:8441,fork,reuseaddr OPENSSL:example.com:443`, with the real host in `Host:`.
- **`select()`/`poll()` return early**: writable counts as ready. Check `FD_ISSET` on the read
  set; sleep when nothing is readable.
- **Don't block in `send()`** on long uploads: `MSG_DONTWAIT`, keep reading between sends.
- **Flycast BBA**: `-config network:EmulateBBA=yes -config network:DCNet=no`. The Dreamcast is
  192.168.169.2 and reaches your LAN through the host. Use the host's LAN address, not
  127.0.0.1 (KOS treats 127/8 as its own).
- **Microphone** (`dc/maple/sip.h`): `maple_enum_type(0, MAPLE_FUNC_MICROPHONE)`,
  `sip_set_frequency(dev, SIP_SAMPLE_11KHZ)`, `sip_set_sample_type(dev, SIP_SAMPLE_16BIT_SIGNED)`,
  `sip_start_sampling(dev, callback, 1)`. The callback runs in the maple driver: copy to a ring
  buffer and return. Flycast: `-config input:device1.1=2`. Tests: `SDL_AUDIODRIVER=disk
  SDL_DISKAUDIOFILEIN=speech.raw` (s16 mono 44100 Hz) feeds it; `SDL_DISKAUDIOFILE=out.raw`
  records output (44100 Hz stereo).
- **Streaming sound** (`dc/sound/stream.h`): `snd_stream_init()`, `snd_stream_alloc()`,
  `snd_stream_start(hnd, rate, 0)`, `snd_stream_poll()` every frame. Coexists with ALdc. The
  callback is asked for bytes, not samples.

## Real hardware

- **Optical drive emulator** (GDEMU, MODE): copy a `make cdi PAD=1` image to the SD card.
- **CD-R**: burn the padded `.cdi` with a DiscJuggler-capable tool. Only MIL-CD consoles (before
  ~late 2000).
- `dc-tool-ip` / `dc-tool-serial` (upload `game.elf` directly) aren't in the kit yet.

## Pinned versions

| | | pinned in |
|---|---|---|
| nixpkgs | `6fcc1603` (26.05) | `shell.nix` |
| Flycast | master `222c9530` (BBA fixes after 2.7) | `shell.nix` |
| mkdcdisc | `d158f773` | `shell.nix` |
| KallistiOS | v2.2.1 `b125c930` + `patches/` | `scripts/dc-bootstrap.sh` |
| sh-elf | GCC 13.2, binutils 2.43, newlib 4.3 | KOS `dc-chain` |
| Simulant | `4a5f5799` (2026-06-17) | `scripts/dc-build-simulant.sh` |

## Why the kit does what it does

- **KOS built with `-m4-single-only`**: Simulant uses it; KOS's default `-m4-single` passes floats
  in other registers (symptom: garbled solid colour, no geometry).
- **`gl1x` forced on PC**: `gl2x`'s shader ignores `set_base_color` without a texture (black).
- **Two host GCCs**: native build uses nixpkgs' default (matches SDL2/OpenAL's libstdc++); GCC 13
  only builds the cross compiler.
- **`SHELL=` passed to dc-chain**: NixOS has no `/bin/bash`.
- **Projects link Simulant's build tree**: its `make install` is broken.
- **`pthread` in the link group**: KOS 2.2 moved it to an add-on.
- **`.cdi`, not `.gdi`**: with `.gdi`, Flycast's BIOS replacement mounts the placeholder track.
- **Assets copied in their own target**: so asset-only changes reach the disc.
- **Flycast from master**: 2.7's BBA hung under two-way streaming (fixed by `c0fb036d`,
  `38a3d83c`, `5543a95c`).
- **KOS patches** (`patches/`):
  - `0001`: `stat()` on `/cd` before any `open()` failed, so no assets were found (upstream `983771f0`).
  - `0002`: every TCP connection used local port 1024 (upstream `ff7f07d2`).
  - `0003`: out-of-order TCP segments corrupted streams, and no window updates made downloads
    crawl (upstream `5cbfe5fd`, `f4c0b536`, idea of `d7f97230`).

## Maintaining the kit

After rebuilding KOS or Simulant, `make clean` in each project.

- **Simulant**: change `SIMULANT_REV` in `scripts/dc-build-simulant.sh`, delete `sdk/build/simulant-*`.
- **KallistiOS**: change `KOS_REV` in `scripts/dc-bootstrap.sh`, delete `sdk/.done/sources`,
  `sdk/.done/patches*`, `sdk/.done/kos-*`, re-run `dc-bootstrap.sh`. Check the patches still
  apply/are needed. If the toolchain profile changed, also delete `sdk/.done/toolchain`.
- **KOS patches**: edit `patches/`, re-run `dc-bootstrap.sh` (it notices, resets KOS, re-applies, rebuilds).
- **nixpkgs**: change `nixpkgsRev`; new hash from `nix-prefetch-url --unpack <tarball url>`.

### Moving the kit

```sh
cd <new location>
sed -i "s|<old location>|$PWD|g" sdk/kos/environ.sh sdk/kos/environ-debug.sh
rm -rf sdk/build/simulant-*      # CMake caches hold the old path; rebuilt on the next make
```

Then fix the path in each project's `shell.nix` and `make clean` there.

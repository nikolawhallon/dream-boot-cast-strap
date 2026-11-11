# dream-boot-cast-strap

This repository is a **kit** for writing 3D games in C++ with the
[Simulant](https://gitlab.com/simulant/simulant) engine. One codebase builds two ways:

- a native **Linux** program, for fast iteration
- a **Dreamcast** disc image (`.cdi`), which boots in the Flycast emulator, on an optical
  drive emulator, or from a burned CD-R on real hardware

The kit holds the toolchain, the SDK, the build scripts and a template. Games live in their own
**project** directories, created from the template, and they all share the kit's SDK.

```
dream-boot-cast-strap/             THE KIT (this repo, cloned anywhere)
├── shell.nix                      dev environment (pinned nixpkgs); projects import it
├── scripts/
│   ├── dc-bootstrap.sh            one time: build the Dreamcast SDK into sdk/
│   ├── dc-build-simulant.sh       build the Simulant engine (Makefiles call it for you)
│   ├── dc-new-project.sh          create a new project from template/
│   ├── dc-picocad-obj.sh          prepare a picoCAD model export for Simulant (section 4)
│   └── dc-screenshot.sh           headless screenshot of a project's game (PC or Dreamcast)
├── patches/                       fixes applied to KallistiOS by dc-bootstrap.sh
├── template/                      starter project (also runnable in place)
└── sdk/                           generated, 1-2 GB (more with Debug builds), never edit by hand
    ├── toolchains/sh-elf/         SH-4 cross compiler (sh-elf-gcc 13.2)
    ├── kos/                       KallistiOS (Dreamcast OS/SDK) + environ.sh
    ├── simulant/                  Simulant engine source (pinned), incl. docs and samples
    └── build/simulant-*/          compiled engine, per target and build type

my-game/                           A PROJECT (anywhere on disk)
├── shell.nix                      1 line: import <kit>/shell.nix
├── Makefile                       make run-pc / make run-dc / ...
├── CMakeLists.txt                 how the game is compiled and linked, per target
├── sources/                       your C++ code (every .cpp in here is compiled)
├── assets/                        your models/textures/sounds (copied next to the binary / onto the disc)
└── build/                         generated
```

---

## 1. Quick start

### Once per machine: build the SDK

Developed and tested on NixOS (x86-64). Nix on another Linux distro should work for building,
but OpenGL programs from Nix (the game, Flycast) usually need a wrapper like `nixGL` there.
The build needs about 5 GB of free disk space (about 1-2 GB remain afterwards).

```sh
git clone https://github.com/nikolawhallon/dream-boot-cast-strap.git
cd dream-boot-cast-strap
nix-shell                 # first run downloads packages
dc-bootstrap.sh           # builds the SH-4 compiler + KallistiOS, 20-40 min
exit
```

Re-running it is harmless, because finished steps are skipped. The SDK lives in `sdk/`
(git-ignored) and contains absolute paths, so build it after cloning to its final location.

### Start a new project

```sh
cd dream-boot-cast-strap && nix-shell
dc-new-project.sh ../my-game "My Game"
exit

cd ../my-game
nix-shell                 # prints "Dreamcast SDK loaded"
make run-pc               # build + run on Linux
make run-dc               # build disc image + boot it in Flycast
```

That's all a new project needs. You get a copy of the template: one textured, unlit 3D model
(a picoCAD warning post) spinning in front of a camera. Edit `sources/` from there.

The first `make run-pc` / `make run-dc` of a given build type compiles Simulant (a few minutes).
After that, every project reuses it.

### Day to day

| Command                       | What it does                                                           |
|-------------------------------|------------------------------------------------------------------------|
| `make run-pc`                 | build + run on Linux                                                   |
| `make run-dc`                 | cross-compile, make `game.cdi`, boot it in Flycast                     |
| `make pc` / `make dc` / `make cdi` | build only                                                        |
| `make shot-pc` / `make shot-dc` | run without a window, save `build/screenshot-*.png` + a `.log` next to it |
| `make clean`                  | delete `build/`                                                        |
| `make TYPE=Debug ...`         | debug build: assertions, debug log, symbols (see section 7)            |
| `make cdi PAD=1`              | padded disc (~700 MB): faster loading on real hardware                 |

In the template, Enter (Linux) or Start (Dreamcast) quits. In Flycast, map your keyboard or
gamepad in its settings (Tab opens the menu).

---

## 2. The big picture

```
 your game (sources/*.cpp)
        │  same code, compiled twice
        ▼
 ┌──────────────────────────────── Simulant engine ────────────────────────────────┐
 │ Application · Scenes · scene graph · assets · materials · input · audio · UI ... │
 │                                                                                  │
 │   Linux build                        │   Dreamcast build                         │
 │   window/input: SDL2                 │   window/input: KallistiOS (maple bus)    │
 │   rendering: OpenGL (gl1x renderer)  │   rendering: GLdc → PowerVR2 GPU          │
 │   audio: OpenAL-soft                 │   audio: ALdc → AICA sound chip           │
 │   libsimulant.so                     │   libsimulant.a (GLdc + ALdc inside)      │
 └──────────────────────────────────────┴───────────────────────────────────────────┘
        │                                         │
   g++ (nixpkgs), x86-64 Linux              KallistiOS v2.2.1 (+ newlib C library)
                                            sh-elf-gcc 13.2 → Hitachi SH-4 machine code
                                                  │
                                            game.elf ─(mkdcdisc)→ game.cdi ─→ Flycast / real DC
```

### The pieces, bottom-up

**Nix (`shell.nix`).** One reproducible environment with every host tool: CMake, Ninja,
SDL2, OpenAL, Flycast, mkdcdisc, and the libraries needed to build the cross compiler. nixpkgs is
pinned to one commit, so the shell is the same every time. Entering the shell also:
- sets `DC_KIT` (the kit dir) and `DC_SDK` (`$DC_KIT/sdk`)
- puts `scripts/` on `PATH`
- sources KallistiOS's `environ.sh`, once it exists. That's what "Dreamcast SDK loaded" means.

**The cross compiler (`sdk/toolchains/sh-elf`).** The Dreamcast CPU is a 200 MHz Hitachi
SH-4. Your PC's gcc can't produce code for it, so the kit builds GCC 13.2 + binutils + newlib
targeting `sh-elf`. KallistiOS's own `dc-chain` scripts do this (called from `dc-bootstrap.sh`).
It's not packaged in nixpkgs, which is why it's a one-time script step rather than a Nix package.

**KallistiOS (`sdk/kos`), "KOS".** The standard open-source Dreamcast SDK, and effectively the
console's operating system. It's linked into your program and provides:
- threads
- a filesystem: `/cd` is the disc, `/pc` is a host link, `/rd` is a ramdisk
- drivers for the controller ports (maple bus), video modes, the PowerVR2 GPU, sound and the modem/BBA
- libc glue

KOS is configured entirely with environment variables (`KOS_BASE`, `KOS_CC_BASE`, ...), which
`sdk/kos/environ.sh` sets.

**GLdc and ALdc.** The Dreamcast has no OpenGL. **GLdc** implements OpenGL 1.x (fixed function,
no shaders) on top of the PowerVR2. **ALdc** implements OpenAL on the AICA sound chip. Both are
written by Simulant's author, and Simulant compiles them straight into `libsimulant.a`.

**Simulant (`sdk/simulant`).** The engine you program against. The same API runs on both
platforms. It has two OpenGL renderers:
- `gl2x`: shaders, the PC default
- `gl1x`: fixed function, what the Dreamcast uses

**The template forces `gl1x` on PC too** (in `main.cpp`), so the PC shows what the console will show.

**CMake + Make (in each project).** `CMakeLists.txt` describes the game for both targets. The
Dreamcast build passes Simulant's toolchain file (`sdk/simulant/toolchains/Dreamcast.cmake`), which
switches the compiler to `sh-elf-g++`, sets SH-4 flags and defines `PLATFORM_DREAMCAST`. The
`Makefile` is a thin wrapper that runs CMake with the right options per target, and builds
Simulant first if it's missing.

**The disc image (`mkdcdisc`).** A Dreamcast boots a disc like this:
1. The BIOS reads the boot sector `IP.BIN` (title, region, the license screen).
2. It loads `1ST_READ.BIN`, your program. On the disc this file is *scrambled*, which is a
   Dreamcast quirk for CD-Rs.

`mkdcdisc` does all of it in one command: it converts `game.elf`, scrambles it, generates
`IP.BIN`, adds `build/<target>/assets/` as `/assets`, and writes a self-booting `.cdi`.
On the console that folder becomes `/cd/assets`, which is one of the game's search paths.

**Flycast.** The emulator. Its built-in "reios" replaces the Dreamcast BIOS, so no BIOS dump
is needed. `make run-dc` passes it the `.cdi`. `make shot-dc` also enables its serial console,
so the game's `printf` and log output end up in `build/screenshot-dc.log`.

### What happens on `make run-dc`

1. If needed, `dc-build-simulant.sh dreamcast Release` builds `sdk/build/simulant-dreamcast-release/simulant/libsimulant.a`.
2. CMake (with `Dreamcast.cmake`) compiles `sources/*.cpp` with `sh-elf-g++`, links `libsimulant.a` + KOS
   into `build/dreamcast-release/game.elf`, and copies `assets/` + Simulant's core assets to
   `build/dreamcast-release/assets/`.
3. `mkdcdisc` writes `build/dreamcast-release/game.cdi`.
4. Flycast boots it: reios → `1ST_READ.BIN` → KOS starts → `main()` → Simulant → your scene.

`make run-pc` does the same with the native compiler and `libsimulant.so`, and runs `build/linux-release/game`.

---

## 3. A project, file by file

| File | Purpose | Edit it? |
|---|---|---|
| `sources/main.cpp` | `main()`: `AppConfig` (title, resolution, renderer, search paths), the `Application` subclass that registers scenes | Rarely: new scenes, config |
| `sources/game_scene.{h,cpp}` | The template's scene: loads `warning.obj`, makes it unlit, spins it | Yes, this is the game |
| `assets/` | Your files (the template has `warning.obj/.mtl` + `warning_tex.png`). Load them by relative name | Yes |
| `CMakeLists.txt` | Compile/link rules for both targets. Every `.cpp`/`.c` under `sources/` is included automatically | Only to add libraries/flags |
| `Makefile` | `make` targets, `TITLE` (disc name) | Change `TITLE` |
| `shell.nix` | Imports the kit's shell | No |

Rebuilding after adding a source file needs nothing special, because the file glob is
re-checked on every build. Assets are copied on every build too, so changing only an asset still
reaches the build and the disc.

---

## 4. Simulant crash course

The template's sources are commented and are the best place to start. The concepts:

- **`Application`**: one per program. Owns the window, main loop, input, audio and the scene
  manager. You subclass it and register scenes in `init()`. The `"main"` scene starts first.
  Switch scenes with `scenes->activate("name")`.
- **`Scene`**: one screen of the game (menu, level...). It's also the **root of a scene graph**.
  Lifecycle hooks are `on_load()` (build things), `on_activate()`/`on_deactivate()`,
  `on_update(dt)` (every frame), `on_fixed_update(step)` (fixed rate, for physics) and `on_unload()`.
- **Nodes**: everything in the world, made with `create_child<T>(...)`: `Actor` (shows a mesh),
  `Camera3D`/`Camera2D`, `DirectionalLight`/`PointLight`, `Skybox`, UI widgets, physics bodies...
  Each has a `transform` (position/orientation/scale; `look_at()`), and nodes can be parented.
  `Params().set("key", value)` passes construction arguments.
  Cameras must be `Camera3D` or `Camera2D`: `create_child<Camera>()` crashes.
- **Layers**: nothing is drawn until you call `compositor->create_layer(subtree, camera)`:
  "draw this subtree, from this camera, into this viewport". Several layers can stack, for
  example a 3D world plus a 2D HUD with its own `Camera2D`.
  **Layers are activated together with the scene.** A layer you create later (when a level or
  match starts) stays inactive and draws nothing until you call `layer->activate()`.
- **Assets**: meshes, materials, textures, sounds, fonts.
  - `assets->...` belongs to the scene and is freed with it. `app->shared_assets->...` lives for the whole program.
  - Load files with `load_texture("assets/x.png")` or `load_mesh("assets/x.obj")`.
    Loaders exist for OBJ, glTF, MD2, Q2 BSP, PNG, JPG, TGA, PCX and more.
  - Build geometry in code with `mesh->create_submesh_as_box/cube/sphere/...`, or vertex by
    vertex (`vertex_data->position(...)`, `tex_coord0(...)`, `color(...)`, `move_next()`, then
    `done()`). Set the colour of every vertex: the default vertex format has a colour that
    multiplies the texture, and it starts as black with zero alpha (invisible).
  - A mesh you rebuild in place keeps its old bounding box, so its actor can be culled away by
    mistake. For things that are always on screen (a HUD), use `actor->set_cullable(false)`.
- **Materials**: how a surface looks. Start with `assets->clone_default_material()`, then
  `set_base_color`, `set_base_color_map(texture)`, `set_lighting_enabled`, `set_blend_func`...
  Loaded models come with their own materials (`for(auto& sm: mesh->each_submesh())
  sm->material()->...`, as in the template).
- **Input**: named axes that combine keyboard, mouse and controllers.
  - Built-in axes: `"Horizontal"`, `"Vertical"`, `"Fire1"`, `"Fire2"`, `"Start"`, `"MouseX"`, `"MouseY"`.
  - Read them with `input->axis_value("Horizontal")` (-1..1) or `axis_was_pressed("Fire1")`.
  - Define your own with `input->new_axis(...)`; `set_joystick_source(GameControllerIndex(n))`
    binds an axis to one controller (for multiplayer).
  - Stick Y: on the Dreamcast, Simulant reports "up" as +1 (its KOS backend flips the axis);
    on PC through SDL, "up" is −1. (Read from the engine's code; not yet checked on hardware.)
- **Logging**: `S_INFO("x = {0}", x)`, `S_DEBUG`, `S_WARN`, `S_ERROR`. On Dreamcast, log lines
  and `printf` go to the serial console.
- **Per-platform code**: `#ifdef __DREAMCAST__`, or at runtime `get_platform()->name() == "dreamcast"`.

### Models from picoCAD

picoCAD's OBJ export needs three fixes before Simulant draws it correctly and quickly.
`dc-picocad-obj.sh <model.obj> <project>/assets` copies the `.obj`, its `.mtl` and texture, and
fixes them:

1. **MTL keys are lowercase** (`map_kd`, `kd`, ...). Simulant's MTL parser is case-sensitive, so
   the model loads untextured. The script writes `map_Kd`, `Kd`, and so on.
2. **`tr 1.0`** means "fully transparent" to Simulant, so it turns on alpha blending for every
   material (slow on the Dreamcast). The script drops the line. (Setting
   `MeshLoadOptions::blending_enabled = false` doesn't help: the OBJ loader ignores it.)
3. **Faces are quads and other polygons.** Simulant's OBJ loader draws each non-triangle face
   as its own draw call, which is very slow. The script splits every face into triangles.

The template's `assets/warning.obj` was made this way. Texture coordinates follow the OBJ
convention (v = 0 at the bottom of the image), which is also what Simulant uses for textures.

Where to learn more:
- `sdk/simulant/documentation/`: the engine's guides (start with `getting-started/`, `scene.md`, `stage_nodes.md`).
  Some pages describe older or newer APIs; when in doubt, the headers win.
- `sdk/simulant/samples/*.cpp`: small complete programs (physics boxes, animated models, UI widgets, viewports, a cave scene).
- `sdk/simulant/simulant/*.h`: the API itself. `simulant.h` includes everything.

---

## 5. Designing for the Dreamcast

Develop on PC, but **check the Dreamcast build often**, because the console is far weaker:

| | Dreamcast | Consequences |
|---|---|---|
| CPU | SH-4 at 200 MHz, fast single-precision FPU | Use `float` everywhere. `double` is emulated in software and very slow. Avoid heavy per-frame allocation. |
| RAM | 16 MB main (your program + data), 8 MB video, 2 MB sound | The whole `.elf` is loaded into RAM. Watch texture sizes. |
| GPU | PowerVR2, fixed function, no shaders, per-vertex lighting | Small power-of-two textures (≤256×256 is a good default). Keep polygons modest in size: huge ones clip badly and light poorly. The GPU itself handles millions of polygons per second; the CPU work around them is the limit (see below). |
| Video | 640×480 | The template uses the same resolution on PC. |
| Input | Controller: analog stick, d-pad, A/B/X/Y, analog L/R triggers, Start | Design for it. The keyboard is the PC stand-in. |
| Disc | CD read speed | Load assets in `on_load()`, not mid-game. |

Fixed-function lighting is computed differently by desktop GL and GLdc, so the two builds can
differ a bit in brightness. **The Dreamcast build is the reference.**

### What things cost

Measured in Flycast while porting a 4-player split-screen game to this kit (relative numbers
are reliable; absolute ones still need checking on real hardware):

| Thing | Cost per frame |
|---|---|
| An empty layer | ~0.02 ms |
| Simulant, per draw call (each actor/submesh, in each layer it's drawn in) | ~35 µs |
| Simulant UI widgets (labels, images) | several draw calls each: four HUDs of ~44 widgets took ~30 ms |
| GLdc, per triangle | ~2.2 µs (≈ 450 CPU cycles) |
| One view of ~6,200 textured triangles through Simulant + GLdc | ~19 ms |
| KallistiOS's bare PowerVR benchmark (`kos/examples/dreamcast/pvr/pvrmark_strips_direct`) | 3.6M+ polygons/s |
| A hand-written PowerVR renderer (transform, clip, cull, submit; see below) | ~94 cycles per vertex (~2M vertices/s) |

What that means in practice:

- **Draw calls are expensive.** Merge small things into one mesh. A HUD built from many UI
  widgets can cost more than the whole 3D world; build it as one mesh of quads from one
  texture atlas instead (text included, with the font baked into the atlas).
- **Split screen multiplies everything:** every view draws its whole scene again.
- **GLdc is fine for modest scenes** (a few thousand triangles at 60 fps). Beyond that, the
  per-triangle cost dominates.
- **The GPU is rarely the limit.** The PowerVR draws screen-space triangles (texturing, depth,
  blending) in hardware, but it does no vertex transformation. Projection, clipping and culling
  run on the SH-4, and that's where the time goes.

### Going around GLdc for the heavy part

For scenes too big for GLdc, a game can draw its 3D world straight to the PowerVR through
KallistiOS's `pvr_*` API and keep Simulant for everything else (input, assets, scene graph,
main loop). Simulant still calls GLdc's `glKosSwapBuffers()` every frame, and GLdc still opens
and closes the PowerVR frame there; the game adds its polygons to that frame. What we learned
doing that:

- **Hook in with the linker.** Link with `-Wl,--wrap=pvr_scene_begin -Wl,--wrap=pvr_list_begin
  -Wl,--wrap=pvr_scene_finish`: GLdc's calls to those KallistiOS functions then go to your
  `__wrap_pvr_list_begin()` etc., which call the originals (`__real_...`) and draw your polygons
  into the list that's open.
- **Wrap KallistiOS functions, not GLdc's.** Simulant (GLdc included) is built with link-time
  optimisation, so calls between GLdc's own functions never reach the linker as symbols and
  `--wrap` can't redirect them. Calls from GLdc into KallistiOS can be.
- **Open each list type at most once per frame.** If GLdc opens the opaque list after you
  already did, everything still works, but it cost ~8.5 ms per frame in testing, and
  KallistiOS only warns about it in debug builds. Draw into GLdc's list when it opens one, and
  open the lists GLdc skips yourself (it skips all of them if Simulant draws nothing).
- **Textures:** load them through Simulant as usual. GLdc uploads them to video memory, and
  `_glGetBoundTexture()` (a GLdc internal: `sdk/simulant/deps/libgl/GL/private.h`) gives the
  address, size and PowerVR format for a `pvr_poly_cxt_txr()` header. Uploads happen lazily, so
  build headers on first draw, not right after loading.
- **Per triangle:** transform with the SH-4's matrix instruction (`mat_load`/`mat_apply`, then
  `mat_trans_nodiv`), reject what's outside the view, clip only what crosses the near plane or a
  view's edge (that also keeps split-screen views apart), cull back faces on the CPU, and write
  vertices through the store queues (`pvr_dr_init`, `pvr_dr_target`, `pvr_dr_commit`). Merge
  duplicate vertices first (Simulant's OBJ loader stores every corner separately) and send
  quads as 4-vertex strips.

The kit doesn't ship such a renderer (yet); this is the recipe.

### Measuring

- `make shot-dc` keeps the Dreamcast's serial output (`printf`, `S_INFO`) in a log next to the
  screenshot. `dc-screenshot.sh dc build/x.png 30` takes it later in the run, with its own log.
- `timer_us_gettime64()` (KallistiOS, microseconds) times anything; `app->stats` has the frame
  rate and frame time; `app->ram_usage_in_bytes()` the memory use; `pvr_get_stats()` the GPU's
  own timings.
- To see where a frame goes, wrap the KallistiOS functions GLdc calls during its swap
  (`pvr_wait_ready`, `pvr_scene_begin`, `pvr_list_begin`/`pvr_list_finish`,
  `pvr_scene_finish`) with `--wrap`, timestamp each, and compare with a timestamp from your
  scene's `on_update()`. That splits a frame into Simulant's work, waiting for the GPU, drawing,
  and GLdc's lists.
- A debug mode that starts the game with bots (no controller needed) lets headless runs
  measure real gameplay. Make sure the bots play like people (e.g. "fire" is a press, not held).

---

## 6. Running on real hardware

- **Optical drive emulator** (GDEMU, MODE, ...): copy `game.cdi` to the SD card. Build it with `make cdi PAD=1`.
- **CD-R**: burn `game.cdi` (padded) with a tool that handles DiscJuggler images. This only boots on
  Dreamcasts that support MIL-CD (consoles made before roughly late 2000).
- **Broadband adapter or coder's cable**: `dc-tool-ip` / `dc-tool-serial` can upload `game.elf` directly.
  They aren't included in the kit yet.

---

## 7. Debug builds

`make run-pc TYPE=Debug`, `make run-dc TYPE=Debug` and so on are separate builds, in
`build/*-debug` plus their own Simulant build. Debug builds:
- enable assertions and the engine's debug log
- keep symbols. On Dreamcast they're split into `game.elf.debug`, because the `.elf` must fit in RAM.

They're slower, especially on the Dreamcast.

---

## 8. Under the hood: versions, patches, decisions

Pinned versions:

| Component | Version | Pinned in |
|---|---|---|
| nixpkgs | `6fcc1603…` (26.05) | `shell.nix` |
| Flycast | 2.7 | via nixpkgs |
| mkdcdisc | `d158f773` (2026-11-08) | `shell.nix` (built as a Nix package) |
| KallistiOS | v2.2.1 (`b125c930`), the version Simulant's own Dreamcast Docker image uses | `scripts/dc-bootstrap.sh` |
| sh-elf toolchain | GCC 13.2.0, binutils 2.43, newlib 4.3.0 (KOS "stable" profile) | KOS `dc-chain` |
| Simulant | `4a5f5799` (main, 2026-06-17) | `scripts/dc-build-simulant.sh` |

Problems found while setting this up, and how the kit handles them:

1. **KOS `stat()` on CD before any `open()` fails.** Simulant looks up every asset with `stat()`,
   so it found no assets on the disc and kept rebooting. `patches/kos-0001-*` backports the
   upstream fix (KOS commit `983771f0`).
2. **Floating-point ABI mismatch.** KOS 2.2 defaults to `-m4-single`, but Simulant compiles with
   `-m4-single-only`. These modes pass `float` arguments in different registers, so every float
   handed from GLdc to KOS was scrambled. The symptom was a solid garbled-color screen with no
   geometry. `dc-bootstrap.sh` builds KOS with `-m4-single-only` (`KOS_PRECISION`).
3. **The PC renderer drew everything black.** Simulant's `gl2x` PBR shader ignores
   `set_base_color` when there's no texture. Fixed by forcing `gl1x` in `main.cpp`.
4. **C++ runtime mismatch.** nixpkgs' OpenAL is built with GCC 15, so the native build uses the
   default GCC (15). GCC 13 (`DC_HOST_CC`) is used only to compile the cross compiler, whose
   GCC 13.2 sources build more reliably with it.
5. **NixOS has no `/bin/bash`.** `dc-chain` hardcodes it, so the bootstrap passes `SHELL=` to make.
6. **Simulant's `make install` is broken** (it references a missing file). Projects link the build
   tree directly (`SIMULANT_SOURCE_DIR` / `SIMULANT_BUILD_DIR` in CMake).
7. **KOS 2.2 moved pthreads into an add-on library.** `CMakeLists.txt` links `pthread`
   explicitly, inside a `--start-group`/`--end-group`.
8. **Use `.cdi`, not `.gdi`, with Flycast's reios.** With `.gdi`, KOS mounts the small placeholder
   track and can't see the game files.
9. **Big polygons on the Dreamcast.** A single 60×60 floor box rendered badly. Build large
   surfaces from tiles (e.g. a 10×10 grid).
10. **Assets were only copied when the code relinked**, so a change to an asset alone never
    reached the build or the disc. The template's `CMakeLists.txt` now copies them in a
    `copy_assets` target that runs on every build.
11. **picoCAD models** load untextured, alpha-blended and slow without fixes; see
    `dc-picocad-obj.sh` (section 4).
12. **`dc-screenshot.sh` wrote every run's log to the same file**, so a second run silently
    replaced the first one's output. The log now sits next to the screenshot, with the same name.

### Updating a component

- **Simulant**: change `SIMULANT_REV` in `scripts/dc-build-simulant.sh`, delete `sdk/build/simulant-*`, then `make clean` in your projects.
- **KallistiOS**: change `KOS_REV` in `scripts/dc-bootstrap.sh`, delete `sdk/.done/sources sdk/.done/patches sdk/.done/kos-*`, then re-run `dc-bootstrap.sh`.
  Check whether the patches are still needed (upstream may already include them), and whether the
  toolchain profile changed. If it did, also delete `sdk/.done/toolchain`.
- **nixpkgs**: change `nixpkgsRev`, then update the hash. Get it with `nix-prefetch-url --unpack <tarball url>`.

After rebuilding KOS or Simulant, run `make clean` in projects. CMake doesn't notice changes to
those libraries by itself.

---

## 9. Troubleshooting

| Symptom | Fix |
|---|---|
| "Run 'nix-shell' first" | Enter the project's `nix-shell`. |
| "Dreamcast SDK not built/loaded" | Run `dc-bootstrap.sh` in the kit's shell, then leave and re-enter `nix-shell`. |
| Dreamcast: "Unable to find file" or the game reboots in a loop | The asset isn't on the disc (is it in `assets/`?), or KOS was rebuilt without `patches/`. |
| Dreamcast: solid single-color screen | Floating-point ABI mismatch (section 8, item 2). Check `KOS_SH4_PRECISION` in `sdk/kos/environ.sh`. |
| PC: things render black | Make sure `force_renderer = "gl1x"` is still set, or give materials a texture. |
| Changes in KOS/Simulant not picked up | `make clean` |
| Want to see what the Dreamcast build prints | `make shot-dc`, then read `build/screenshot-dc.log` |
| A model shows up untextured (white or grey) | Lowercase MTL keys (picoCAD): run it through `dc-picocad-obj.sh`. |
| A layer draws nothing (screen stays black) | It was created after the scene activated: call `layer->activate()`. |
| A mesh built in code is invisible | Set every vertex's colour (`vertex_data->color(...)`); for meshes rebuilt in place, also `set_cullable(false)`. |
| Crash when creating a camera | Use `Camera3D` or `Camera2D`, not `Camera`. |
| Dreamcast: slow with many objects or split screen | Section 5, "What things cost": fewer draw calls, triangulated models, one-mesh HUDs. |
| Linker errors about `pthread_*` or KOS symbols | Keep the link group in `CMakeLists.txt` intact. |

Start over completely: delete `sdk/`, then run `dc-bootstrap.sh` again. That takes 20-40 minutes plus the Simulant builds.

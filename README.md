# dream-boot-cast-strap

A kit for making 3D games in C++ with the [Simulant](https://gitlab.com/simulant/simulant)
engine. One codebase builds:

- a **Linux** program (fast iteration)
- a **Dreamcast** disc image (`.cdi`) for Flycast, an optical drive emulator, or a CD-R

The kit holds the toolchain, the SDK, build scripts, a renderer (`dbcs`) and a project
template. Each game is its own directory and shares the kit's SDK.

More detail (performance numbers, networking, real hardware, kit maintenance): [NOTES.md](NOTES.md).

## Setup (once per machine)

Needs Nix (tested on NixOS x86-64; other distros need `nixGL` for OpenGL programs) and ~5 GB
of disk during the build.

```sh
git clone https://github.com/nikolawhallon/dream-boot-cast-strap.git
cd dream-boot-cast-strap
nix-shell
dc-bootstrap.sh        # SH-4 compiler + KallistiOS, 20-40 min. Safe to re-run.
exit
```

The SDK goes in `sdk/` and stores absolute paths: clone the kit where it will stay.

## New game

```sh
cd dream-boot-cast-strap && nix-shell
dc-new-project.sh ../my-game "My Game"
exit

cd ../my-game && nix-shell     # prints "Dreamcast SDK loaded"
make run-pc                    # Linux
make run-dc                    # Dreamcast, in Flycast
```

The first build of each type compiles Simulant (a few minutes); later projects reuse it.
The template shows one textured model spinning. Enter (PC) / Start (Dreamcast) quits.

## Commands

| | |
|---|---|
| `make run-pc` | build + run on Linux |
| `make run-dc` | build disc image + boot it in Flycast |
| `make pc` / `make dc` / `make cdi` | build only |
| `make shot-pc` / `make shot-dc` | headless screenshot + log in `build/`. `WAIT=X`: X seconds after launch (default 5 / 15) |
| `make clean` | delete `build/`. Do this after the SDK or Simulant changes |
| `TYPE=Debug` | debug build (assertions, debug log, symbols), separate from release |
| `PAD=1` | padded disc (~700 MB), loads faster on real hardware |

In Flycast, Tab opens the menu (map keyboard/gamepad there).

## A project

```
my-game/
├── sources/            your code; every .cpp/.c is compiled
│   ├── main.cpp        AppConfig (title, resolution, search paths), registers scenes
│   └── game_scene.*    the game
├── assets/             loaded by relative name; copied next to the binary and onto the disc
├── Makefile            TITLE = disc name
├── CMakeLists.txt      only edit to add libraries or flags
└── shell.nix           imports the kit's shell
```

On the Dreamcast, `assets/` is at `/cd/assets`. Assets are copied on every build.

## Simulant essentials

- **`Application`**: one per program. Register scenes in `init()`; `"main"` starts first.
  `scenes->activate("name")` switches.
- **`Scene`**: one screen (menu, level). Hooks: `on_load()`, `on_activate()`,
  `on_update(dt)`, `on_fixed_update(step)`, `on_unload()`. It's also the root node.
- **Nodes**: `create_child<Actor>(mesh)`, `Camera3D`, `Camera2D`, lights... Each has a
  `transform` (`set_position`, `set_orientation`, `look_at`). Use `Camera3D`/`Camera2D`,
  never `Camera` (crashes).
- **Assets**: `assets->load_mesh("x.obj")`, `load_texture("x.png")` (scene lifetime);
  `app->shared_assets` lives for the whole program. OBJ, glTF, MD2, PNG, JPG, TGA...
- **Meshes in code**: set every vertex's colour (`vertex_data->color(...)`), the default is
  transparent black.
- **Materials**: `assets->clone_default_material()`, then `set_base_color_map(tex)`,
  `set_lighting_enabled(false)`, `set_blend_func(BLEND_NONE)`.
- **Input**: `input->axis_value("Horizontal")` (-1..1), `axis_was_pressed("Fire1")`. Built-in
  axes: `Horizontal`, `Vertical`, `Fire1`, `Fire2`, `Start`. Per controller:
  `new_axis(...)` + `set_joystick_source(GameControllerIndex(n))`. Stick up is +1 on Dreamcast,
  −1 on PC.
- **Logging**: `S_INFO("x = {0}", x)`, `S_WARN`, `S_ERROR`. On Dreamcast it goes to serial,
  which is slow: never log every frame.
- **Platform code**: `#ifdef __DREAMCAST__`.

Reference: `sdk/simulant/simulant/*.h` (the API; `simulant.h` includes all),
`sdk/simulant/samples/`, `sdk/simulant/documentation/` (partly outdated; headers win).

## Drawing: dbcs

`lib/dbcs` draws your scene. Same code on both builds; on Linux it uses Simulant, on the
Dreamcast it goes straight to the PowerVR (Simulant's own path is several times slower).
Don't create Simulant layers yourself.

```cpp
#include "dbcs/renderer.h"

// on_load():
renderer_.reset(new dbcs::Renderer(this));      // one per scene, keep it
auto world = create_child<Stage>();              // Actors directly under it are drawn
auto camera = create_child<Camera3D>();
camera->set_perspective_projection(Degrees(60), renderer_->aspect_ratio(Viewport()), 0.5f, 100.0f);
renderer_->add_view(world, camera, Viewport(), {Color::black()});   // 2+ colours = gradient

// 2D: textured quads over a viewport, alpha-blended, on top of the views
dbcs::Overlay* hud = renderer_->add_overlay(Viewport(), atlas_texture, /*order*/ 1);
hud->clear();
hud->add({x, y, w, h, u0, v0, u1, v1});          // pixels, origin bottom-left
hud->commit();
```

- Split screen: one `add_view()` per viewport.
- Overlays draw by `order`, and quads in order within an overlay. Enough for a whole 2D game
  (tile map, sprites, text from a font baked into an atlas). Rebuilding a few thousand quads per
  frame is fine.
- Limits: only **direct** children of the world node; unlit, opaque; no animation; mesh
  changes after the first draw don't show on Dreamcast. Keep materials unlit and opaque so
  Linux matches.

Full API: `lib/dbcs/renderer.h`.

## Dreamcast limits

| | |
|---|---|
| CPU | SH-4 200 MHz. Use `float`; `double` is emulated. Avoid per-frame allocation. |
| RAM | 16 MB (the whole `.elf` + data), 8 MB video, 2 MB sound |
| GPU | No shaders. Power-of-two textures, ≤256×256. Split large surfaces into tiles. |
| Screen | 640×480 (PC uses the same) |
| Disc | Slow. Load in `on_load()`. |

The Dreamcast build is the reference: test it often. CPU work per triangle is the usual limit,
not the GPU. See [NOTES.md](NOTES.md) for costs, measuring, networking and the microphone.

## Troubleshooting

| Symptom | Fix |
|---|---|
| "Run 'nix-shell' first" | Enter the project's `nix-shell`. |
| "Dreamcast SDK not built/loaded" | `dc-bootstrap.sh` in the kit's shell, then re-enter `nix-shell`. |
| Dreamcast reboots in a loop / "Unable to find file" | The file isn't in `assets/`. |
| Dreamcast: solid single-colour screen | KOS built with the wrong float ABI: `KOS_SH4_PRECISION` in `sdk/kos/environ.sh` must be `-m4-single-only`. |
| PC renders black | Keep `force_renderer = "gl1x"` in `main.cpp`. |
| Mesh built in code invisible | Set vertex colours. |
| SDK/Simulant changes not picked up | `make clean` |
| What did the Dreamcast print? | `make shot-dc`, read `build/screenshot-dc.log` |
| Slow on Dreamcast | Fewer actors/submeshes, triangulated models, one overlay per HUD. |
| Start over | Delete `sdk/`, run `dc-bootstrap.sh`. |

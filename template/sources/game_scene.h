#pragma once

#include <memory>

#include <simulant/simulant.h>

#include "dbcs/renderer.h"

// The smallest useful 3D scene: one textured model (assets/warning.obj, a picoCAD model),
// unlit, spinning in front of a camera.
//   Linux: Enter quits.   Dreamcast: Start quits.
//
// A Scene is one "screen" of the game (title screen, level, game over...). It is also the
// root node of a scene graph: everything you create_child<>() lives under it, and is
// destroyed with it.
//
// Lifecycle (override what you need):
//   on_load()       build the scene: cameras, meshes, what to draw
//   on_activate()   the scene becomes the active one;  on_deactivate() when it stops being active
//   on_update(dt)   once per frame, dt = seconds since the last frame (game logic, input)
//   on_fixed_update(step)  at a fixed rate (physics)
//   on_unload()     clean-up
class GameScene : public smlt::Scene {
public:
    GameScene(smlt::Window* window) :
        smlt::Scene(window) {}

    void on_load() override;
    void on_update(float dt) override;

private:
    std::unique_ptr<dbcs::Renderer> renderer_;
    smlt::Actor* model_ = nullptr;
    float angle_ = 0.0f;   // degrees around Y
};

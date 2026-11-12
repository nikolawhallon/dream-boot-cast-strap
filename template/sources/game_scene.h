#pragma once

#include <memory>

#include <simulant/simulant.h>

#include "dbcs/renderer.h"

// One textured model spinning in front of a camera.
// Scene hooks you can override: on_load, on_activate, on_deactivate, on_update, on_fixed_update, on_unload.
class GameScene : public smlt::Scene {
public:
    GameScene(smlt::Window* window) :
        smlt::Scene(window) {}

    void on_load() override;
    void on_update(float dt) override;

private:
    std::unique_ptr<dbcs::Renderer> renderer_;
    smlt::Actor* model_ = nullptr;
    float angle_ = 0.0f;
};

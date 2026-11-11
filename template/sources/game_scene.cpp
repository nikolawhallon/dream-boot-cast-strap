#include "game_scene.h"

#include <cmath>

using namespace smlt;

namespace {

// A material describes how a surface is drawn: base color, texture(s), lighting, blending...
// Start from the engine's default material and tweak it.
MaterialPtr colored_material(AssetManager* assets, const Color& color) {
    auto mat = assets->clone_default_material();
    mat->set_base_color(color);
    mat->set_lighting_enabled(true);
    return mat;
}

} // namespace

void GameScene::on_load() {
    // --- Camera + layer -------------------------------------------------------------------
    // A camera is a node in the scene. A *layer* (created by the compositor) is what actually
    // gets drawn: "render this part of the scene graph, seen from this camera, into a viewport".
    // No layer = nothing on screen.
    camera_ = create_child<Camera3D>();
    camera_->set_perspective_projection(
        Degrees(60.0f), float(window->width()) / float(window->height()), 0.5f, 200.0f);

    layer_ = compositor->create_layer(this, camera_);
    layer_->viewport->set_color(Color(0.2f, 0.25f, 0.4f, 1.0f));   // background ("sky")
    layer_->set_clear_flags(BUFFER_CLEAR_ALL);

    // --- Meshes and actors ----------------------------------------------------------------
    // A Mesh is geometry (made of submeshes, each with its own material). It's an *asset*:
    // created once, shared. An Actor is a node that places a mesh in the world; many actors
    // can show the same mesh. `assets` belongs to this scene (freed with it); app->shared_assets
    // lives for the whole program.

    // Floor: a checkerboard of small tiles rather than one huge box. Big polygons that
    // cross the camera's near plane break on the Dreamcast (GLdc clipping), and vertex
    // lighting needs reasonably small polygons to look right anyway.
    const int tiles = 10;
    const float tile = 6.0f;
    auto floor_mesh = assets->create_mesh(VertexSpecification::DEFAULT);
    auto light_tile = colored_material(assets.get(), Color(0.3f, 0.6f, 0.3f, 1.0f));
    auto dark_tile = colored_material(assets.get(), Color(0.2f, 0.45f, 0.2f, 1.0f));
    for(int x = 0; x < tiles; ++x) {
        for(int z = 0; z < tiles; ++z) {
            Vec3 offset((x - tiles / 2 + 0.5f) * tile, 0.0f, (z - tiles / 2 + 0.5f) * tile);
            floor_mesh->create_submesh_as_box(
                "tile_" + std::to_string(x) + "_" + std::to_string(z),
                ((x + z) % 2) ? light_tile : dark_tile, tile, 1.0f, tile, offset);
        }
    }
    auto floor = create_child<Actor>(floor_mesh);
    floor->transform->set_position(Vec3(0, -0.5f, 0));   // top surface at y = 0

    // Pillars in a ring: ONE mesh, twelve actors
    auto pillar_mesh = assets->create_mesh(VertexSpecification::DEFAULT);
    pillar_mesh->create_submesh_as_box(
        "pillar", colored_material(assets.get(), Color(0.8f, 0.7f, 0.5f, 1.0f)), 1.5f, 4.0f, 1.5f);
    const int pillars = 12;
    for(int i = 0; i < pillars; ++i) {
        float a = (float(i) / pillars) * 2.0f * float(PI);
        auto p = create_child<Actor>(pillar_mesh);
        p->transform->set_position(Vec3(std::cos(a) * 15.0f, 2.0f, std::sin(a) * 15.0f));
    }

    // Player cube
    auto cube_mesh = assets->create_mesh(VertexSpecification::DEFAULT);
    cube_mesh->create_submesh_as_cube(
        "player", colored_material(assets.get(), Color(0.9f, 0.3f, 0.2f, 1.0f)), 1.5f);
    player_ = create_child<Actor>(cube_mesh);
    player_->transform->set_position(Vec3(0, 0.75f, 0));

    // --- Lighting -------------------------------------------------------------------------
    // Ambient light hits everything equally. Lights are nodes too; Params passes
    // construction arguments to create_child<>().
    lighting->set_ambient_light(Color(0.35f, 0.35f, 0.35f, 1.0f));
    create_child<DirectionalLight>(
        Params()
            .set("direction", Vec3(-1.0f, -2.0f, -1.0f))
            .set("color", Color(1.0f, 0.95f, 0.85f, 1.0f)));
}

void GameScene::on_update(float dt) {
    Scene::on_update(dt);

    // --- Input ----------------------------------------------------------------------------
    // Simulant maps keyboard, mouse and controllers onto named *axes*. Built-in ones:
    //   "Horizontal" A/D, Left/Right, stick X     "Vertical" W/S, Up/Down, stick Y
    //   "Fire1" LCtrl / A button / left mouse      "Fire2" RAlt / B button / right mouse
    //   "Start" Enter / Start button
    // axis_value() is -1..1 (analog); axis_value_hard() is -1, 0 or 1.
    if(input->axis_value_hard("Start") == 1) {
        app->stop_running();
        return;
    }

    const float turn_speed = 120.0f; // deg/s
    const float move_speed = 8.0f;   // units/s

    float turn = input->axis_value("Horizontal");
    float throttle = input->axis_value("Vertical");
#ifdef __DREAMCAST__
    // DC stick Y is +down, keyboard W is +up: flip so "up" drives forward on both
    throttle = -throttle;
#endif

    // --- Movement (multiply by dt so speed doesn't depend on frame rate) ------------------
    heading_ -= turn * turn_speed * dt;
    float rad = heading_ * float(PI) / 180.0f;
    Vec3 forward(-std::sin(rad), 0.0f, -std::cos(rad));   // -Z is "forward" in Simulant

    auto pos = player_->transform->position() + forward * (throttle * move_speed * dt);
    pos.x = std::max(-28.0f, std::min(28.0f, pos.x));
    pos.z = std::max(-28.0f, std::min(28.0f, pos.z));
    player_->transform->set_position(pos);

    // Face the travel direction, and roll while moving
    spin_ += throttle * dt * 360.0f;
    player_->transform->set_orientation(
        Quaternion(Vec3::up(), Degrees(heading_)) *
        Quaternion(Vec3(1, 0, 0), Degrees(-spin_)));

    // --- Chase camera: behind and above the player, looking at it -------------------------
    camera_->transform->set_position(pos - forward * 8.0f + Vec3(0, 4.0f, 0));
    camera_->transform->look_at(pos + Vec3(0, 0.5f, 0));
}

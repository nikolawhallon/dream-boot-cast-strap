#include "game_scene.h"

using namespace smlt;

void GameScene::on_load() {
    // dbcs draws views: a world (every Actor directly under it), a camera, a viewport, a background.
    renderer_.reset(new dbcs::Renderer(this));
    auto world = create_child<Stage>();

    auto camera = create_child<Camera3D>();
    camera->set_perspective_projection(Degrees(60.0f), renderer_->aspect_ratio(Viewport()), 0.5f, 100.0f);
    camera->transform->set_position(Vec3(0, 3, 10));
    camera->transform->look_at(Vec3(0, 2, 0));

    // Viewport() = whole screen. Background: one colour, or several for a vertical gradient.
    renderer_->add_view(world, camera, Viewport(), {Color(0.11f, 0.17f, 0.33f, 1.0f)});

    // A picoCAD model (see "picoCAD models" in the kit README). The .mtl names the texture.
    MeshLoadOptions options;
    options.cull_mode = CULL_MODE_BACK_FACE;
    auto mesh = assets->load_mesh("warning.obj", VertexSpecification::DEFAULT, options);

    // Unlit and opaque, so Linux matches the Dreamcast.
    for(auto& submesh: mesh->each_submesh()) {
        submesh->material()->set_lighting_enabled(false);
        submesh->material()->set_blend_func(BLEND_NONE);
    }

    model_ = world->create_child<Actor>(mesh);
}

void GameScene::on_update(float dt) {
    Scene::on_update(dt);

    // Enter on PC, Start on Dreamcast
    if(input->axis_value_hard("Start") == 1) {
        app->stop_running();
        return;
    }

    angle_ += 45.0f * dt;   // degrees per second
    model_->transform->set_orientation(Quaternion(Vec3::up(), Degrees(angle_)));
}

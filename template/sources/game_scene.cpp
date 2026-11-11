#include "game_scene.h"

using namespace smlt;

void GameScene::on_load() {
    // --- What to draw ---------------------------------------------------------------------
    // The kit's renderer (dbcs, see the kit README) draws VIEWS: "this world, seen from this
    // camera, in this part of the screen, over this background". On Linux it uses Simulant's
    // own renderer; on the Dreamcast it draws straight to the PowerVR, which is much faster.
    // Your code is the same for both.
    //
    // A world is just a node: every Actor directly under it gets drawn.
    renderer_.reset(new dbcs::Renderer(this));
    auto world = create_child<Stage>();

    auto camera = create_child<Camera3D>();
    camera->set_perspective_projection(Degrees(60.0f), renderer_->aspect_ratio(Viewport()), 0.5f, 100.0f);
    camera->transform->set_position(Vec3(0, 3, 10));
    camera->transform->look_at(Vec3(0, 2, 0));

    // Viewport() = the whole screen. Background: one colour (several = a vertical gradient).
    renderer_->add_view(world, camera, Viewport(), {Color(0.11f, 0.17f, 0.33f, 1.0f)});

    // --- The model ------------------------------------------------------------------------
    // A Mesh is geometry plus materials. It's an *asset*: loaded once, shared. An Actor is a
    // node that places a mesh in the world; many actors can show the same mesh.
    //
    // assets/warning.obj was prepared with `dc-picocad-obj.sh` (see the kit's README): picoCAD
    // exports need their MTL keys fixed and their faces split into triangles for Simulant.
    // The texture is found through the .mtl file (map_Kd warning_tex.png).
    MeshLoadOptions options;
    options.cull_mode = CULL_MODE_BACK_FACE;   // don't draw the inside faces
    auto mesh = assets->load_mesh("warning.obj", VertexSpecification::DEFAULT, options);

    // No lighting: each pixel is just its texture colour. Opaque: no blending.
    // (On the Dreamcast the renderer is always unlit and opaque; this makes Linux match.)
    for(auto& submesh: mesh->each_submesh()) {
        submesh->material()->set_lighting_enabled(false);
        submesh->material()->set_blend_func(BLEND_NONE);
    }

    model_ = world->create_child<Actor>(mesh);
}

void GameScene::on_update(float dt) {
    Scene::on_update(dt);

    // --- Input ----------------------------------------------------------------------------
    // Simulant maps keyboard, mouse and controllers onto named *axes*. Built-in ones:
    //   "Horizontal", "Vertical" (WASD/arrows, stick), "Fire1", "Fire2", "Start" (Enter / Start)
    if(input->axis_value_hard("Start") == 1) {
        app->stop_running();
        return;
    }

    // --- Spin (multiply by dt so the speed doesn't depend on the frame rate) --------------
    angle_ += 45.0f * dt;
    model_->transform->set_orientation(Quaternion(Vec3::up(), Degrees(angle_)));
}

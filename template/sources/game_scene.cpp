#include "game_scene.h"

using namespace smlt;

void GameScene::on_load() {
    // --- Camera + layer -------------------------------------------------------------------
    // A camera is a node in the scene. A *layer* (created by the compositor) is what actually
    // gets drawn: "render this part of the scene graph, seen from this camera, into a viewport".
    // No layer = nothing on screen.
    //
    // Layers are activated when the scene is. A layer you create LATER (say, when a level
    // starts) is inactive and draws nothing until you call layer->activate().
    auto camera = create_child<Camera3D>();
    camera->set_perspective_projection(
        Degrees(60.0f), float(window->width()) / float(window->height()), 0.5f, 100.0f);
    camera->transform->set_position(Vec3(0, 3, 10));
    camera->transform->look_at(Vec3(0, 2, 0));

    auto layer = compositor->create_layer(this, camera);
    layer->viewport->set_color(Color(0.11f, 0.17f, 0.33f, 1.0f));   // background
    layer->set_clear_flags(BUFFER_CLEAR_ALL);

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
    for(auto& submesh: mesh->each_submesh()) {
        submesh->material()->set_lighting_enabled(false);
        submesh->material()->set_blend_func(BLEND_NONE);
    }

    model_ = create_child<Actor>(mesh);
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

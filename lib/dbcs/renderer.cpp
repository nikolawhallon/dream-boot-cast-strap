// dbcs::Renderer: the parts shared by both platforms, and the Linux implementation
// (plain Simulant). The Dreamcast implementation is in renderer_dc.cpp.
#include "renderer.h"

#include <algorithm>

using namespace smlt;

namespace dbcs {

// --- Shared --------------------------------------------------------------------------------

void Renderer::pixels(const Viewport& vp, float& x, float& y, float& w, float& h) const {
    // Viewport ratios have their origin bottom-left; pixels here are top-left based
    float sw = float(scene_->window->width()), sh = float(scene_->window->height());
    x = vp.x() * sw;
    y = (1.0f - vp.y() - vp.height()) * sh;
    w = vp.width() * sw;
    h = vp.height() * sh;
}

float Renderer::aspect_ratio(const Viewport& vp) const {
    float x, y, w, h;
    pixels(vp, x, y, w, h);
    return w / h;
}

void Renderer::add_view(StageNode* world, Camera3D* camera, const Viewport& viewport,
                        const std::vector<Color>& background) {
    platform_add_view(world, camera, viewport,
                      background.empty() ? std::vector<Color>{Color::black()} : background);
}

Overlay* Renderer::add_overlay(const Viewport& viewport, TexturePtr texture, int order) {
    overlays_.emplace_back(new Overlay());
    Overlay* o = overlays_.back().get();
    o->order_ = order;
    pixels(viewport, o->x_, o->y_, o->w_, o->h_);
    platform_add_overlay(o, viewport, texture);
    return o;
}

void Renderer::remove_overlay(Overlay* overlay) {
    auto it = std::find_if(overlays_.begin(), overlays_.end(),
                           [&](const std::unique_ptr<Overlay>& o) { return o.get() == overlay; });
    if(it == overlays_.end()) return;
    platform_remove_overlay(overlay);
    overlays_.erase(it);
}

void Renderer::set_overlay_texture(Overlay* overlay, TexturePtr texture) {
    platform_set_overlay_texture(overlay, texture);
}

} // namespace dbcs

#ifndef __DREAMCAST__

// --- Linux: Simulant layers ----------------------------------------------------------------
//
// A view is two compositor layers limited to its viewport: the background (a quad with
// per-vertex colours, seen by an orthographic camera) and the world (seen by the view's
// camera). An overlay is a mesh under its own Stage, drawn by a layer with a pixel-sized
// orthographic camera.

namespace dbcs {

struct Renderer::Impl {
    Camera2D* background_camera = nullptr;     // sees 0..1 x 0..1
    std::vector<LayerPtr> view_layers;
    std::vector<Stage*> background_stages;
};

struct Overlay::Impl {
    Stage* root = nullptr;
    Camera2D* camera = nullptr;
    LayerPtr layer;
    MeshPtr mesh;
    SubMeshPtr submesh;
};

namespace {

// Layers are activated when their scene is. Ones created later (a match starting...) need
// activate() or they never draw.
LayerPtr new_layer(Scene* scene, StageNode* subtree, Camera* camera, const Viewport& viewport,
                   int priority, uint32_t clear_flags) {
    auto layer = scene->compositor->create_layer(subtree, camera, priority);
    layer->set_viewport(viewport);
    layer->set_clear_flags(clear_flags);
    layer->activate();
    return layer;
}

MaterialPtr unlit(Scene* scene, TexturePtr texture, bool blend) {
    auto mat = scene->assets->clone_default_material();
    mat->set_lighting_enabled(false);
    mat->set_base_color(Color::white());
    if(texture) mat->set_base_color_map(texture);
    mat->set_blend_func(blend ? BLEND_ALPHA : BLEND_NONE);
    mat->set_depth_test_enabled(false);
    mat->set_depth_write_enabled(false);
    mat->set_cull_mode(CULL_MODE_NONE);
    return mat;
}

} // namespace

bool Renderer::direct() { return false; }

Renderer::Renderer(Scene* scene) : scene_(scene), impl_(new Impl()) {
    impl_->background_camera = scene->create_child<Camera2D>();
    impl_->background_camera->set_orthographic_projection(0, 1, 0, 1, -1, 1);
}

Renderer::~Renderer() {
    clear_views();
    while(!overlays_.empty()) remove_overlay(overlays_.back().get());
}

void Renderer::platform_add_view(StageNode* world, Camera3D* camera, const Viewport& viewport,
                                 const std::vector<Color>& background) {
    // Background: a strip of quads from the bottom (first colour) to the top (last colour)
    auto mesh = scene_->assets->create_mesh(VertexSpecification::POSITION_AND_DIFFUSE);
    auto sm = mesh->create_submesh("background", unlit(scene_, nullptr, false), INDEX_TYPE_16_BIT);
    auto vdata = mesh->vertex_data.get();
    const int rows = std::max(1, int(background.size()) - 1);
    for(int r = 0; r <= rows; ++r) {
        const Color& c = background[std::min<size_t>(r, background.size() - 1)];
        for(int side = 0; side < 2; ++side) {
            vdata->position(float(side), float(r) / rows, 0.0f);
            vdata->color(c);
            vdata->move_next();
        }
    }
    vdata->done();
    for(int r = 0; r < rows; ++r) {
        uint32_t i = r * 2;
        sm->index_data->index(i); sm->index_data->index(i + 1); sm->index_data->index(i + 2);
        sm->index_data->index(i + 1); sm->index_data->index(i + 3); sm->index_data->index(i + 2);
    }
    sm->index_data->done();

    auto stage = scene_->create_child<Stage>();
    stage->create_child<Actor>(mesh);
    impl_->background_stages.push_back(stage);

    impl_->view_layers.push_back(new_layer(scene_, stage, impl_->background_camera, viewport,
                                           -10, BUFFER_CLEAR_ALL));
    impl_->view_layers.push_back(new_layer(scene_, world, camera, viewport,
                                           0, BUFFER_CLEAR_DEPTH_BUFFER));
}

void Renderer::clear_views() {
    for(auto& layer: impl_->view_layers) layer->destroy();
    for(auto stage: impl_->background_stages) stage->destroy();
    impl_->view_layers.clear();
    impl_->background_stages.clear();
}

void Renderer::platform_add_overlay(Overlay* o, const Viewport& viewport, TexturePtr texture) {
    o->impl_ = std::make_shared<Overlay::Impl>();
    auto& p = *o->impl_;
    p.root = scene_->create_child<Stage>();
    p.camera = scene_->create_child<Camera2D>();
    p.camera->set_orthographic_projection(0, o->w_, 0, o->h_, -1, 1);
    // Layers of equal priority draw in creation order, which is what `order` promises
    p.layer = new_layer(scene_, p.root, p.camera, viewport, 100 + o->order_, 0);

    p.mesh = scene_->assets->create_mesh(VertexSpecification::DEFAULT);
    p.submesh = p.mesh->create_submesh("overlay", unlit(scene_, texture, true), INDEX_TYPE_16_BIT);
    // The mesh is rebuilt in place, so its bounding box is stale: never cull it
    p.root->create_child<Actor>(p.mesh)->set_cullable(false);
}

void Renderer::platform_remove_overlay(Overlay* o) {
    auto& p = *o->impl_;
    p.layer->destroy();
    p.root->destroy();
    p.camera->destroy();
}

void Renderer::platform_set_overlay_texture(Overlay* o, TexturePtr texture) {
    o->impl_->submesh->material()->set_base_color_map(texture);
}

void Overlay::commit() {
    auto vdata = impl_->mesh->vertex_data.get();
    auto idata = impl_->submesh->index_data.get();
    vdata->clear();
    idata->clear();
    uint32_t i = 0;
    for(const Quad& q: quads_) {
        const float corners[4][4] = {
            {q.x, q.y, q.u0, q.v0}, {q.x + q.w, q.y, q.u1, q.v0},
            {q.x + q.w, q.y + q.h, q.u1, q.v1}, {q.x, q.y + q.h, q.u0, q.v1},
        };
        for(auto& c: corners) {
            vdata->position(c[0], c[1], 0.0f);
            vdata->normal(0, 0, 1);
            vdata->tex_coord0(c[2], c[3]);
            vdata->color(Color::white());   // multiplies the texture: unset = black, alpha 0
            vdata->move_next();
        }
        idata->index(i); idata->index(i + 1); idata->index(i + 2);
        idata->index(i); idata->index(i + 2); idata->index(i + 3);
        i += 4;
    }
    vdata->done();
    idata->done();
}

uint32_t Renderer::triangles() const { return 0; }
uint32_t Renderer::draw_us() const { return 0; }
uint32_t Renderer::overlay_us() const { return 0; }
uint32_t Renderer::overlay_quads() const { return 0; }

} // namespace dbcs

#endif

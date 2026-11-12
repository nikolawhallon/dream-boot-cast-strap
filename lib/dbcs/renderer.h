#pragma once

// dbcs::Renderer -- one drawing API for both builds of a kit game.
//
// You describe what's on screen: VIEWS (a 3D world seen from a camera, drawn into a viewport,
// with a background) and OVERLAYS (2D textured quads over a viewport, e.g. a HUD). The
// renderer draws them:
//
//   Linux:     with Simulant (compositor layers), like any Simulant game
//   Dreamcast: straight to the PowerVR GPU, bypassing Simulant's renderer and GLdc, which are
//              far too slow for split screen or a few thousand triangles (see the kit NOTES.md)
//
// Your game code is the same for both. Simulant still does everything else (window, input,
// assets, scene graph, main loop).
//
// What gets drawn in a view: every visible Actor that is a DIRECT child of the view's world
// node, with its mesh. Materials: the texture (or none) and back-face culling; no lighting,
// no blending. Everything is unlit.
//
// Limits (for now):
//   - only direct children of the world node, only their base mesh, no animation
//   - meshes are converted once, the first time they're drawn: changing a mesh's vertices
//     later doesn't show up on the Dreamcast
//   - one Renderer at a time (it hooks into the Dreamcast's frame)
//
// Use it from CMake with:   include($ENV{DC_KIT}/lib/dbcs/dbcs.cmake)   dbcs_use(game)

#include <cstdint>
#include <memory>
#include <vector>

#include <simulant/simulant.h>

namespace dbcs {

// One textured quad of an overlay, in pixels inside the overlay's viewport
// (origin bottom-left, y up). Texture coordinates: (u0, v0) bottom-left, (u1, v1) top-right,
// with v = 0 at the bottom of the image (Simulant's convention).
struct Quad {
    float x, y, w, h;
    float u0, v0, u1, v1;
};

class Renderer;

// A list of quads over a viewport, all from one texture. Fill it, then commit():
//   overlay->clear(); overlay->add({...}); ...; overlay->commit();
class Overlay {
public:
    void clear() { quads_.clear(); }
    void add(const Quad& quad) { quads_.push_back(quad); }
    void commit();   // call after changing the quads

    float width() const { return w_; }     // the viewport's size in pixels
    float height() const { return h_; }

    // Used by the renderer
    const std::vector<Quad>& quads() const { return quads_; }
    float screen_x() const { return x_; }  // the viewport's top-left corner in screen pixels
    float screen_y() const { return y_; }

private:
    friend class Renderer;
    struct Impl;
    std::vector<Quad> quads_;
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;  // viewport in screen pixels, origin top-left
    int order_ = 0;
    std::shared_ptr<Impl> impl_;           // platform data
};

class Renderer {
public:
    // Create in Scene::on_load(), keep it for the scene's lifetime.
    explicit Renderer(smlt::Scene* scene);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // True on the Dreamcast (draws to the PowerVR), false on Linux (draws with Simulant).
    static bool direct();

    // Draw `world` from `camera` into `viewport`. `background` is painted behind it: one colour
    // = solid, several = a vertical gradient from the bottom (first) to the top (last).
    // Set the camera's projection yourself; aspect_ratio() helps.
    void add_view(smlt::StageNode* world, smlt::Camera3D* camera, const smlt::Viewport& viewport,
                  const std::vector<smlt::Color>& background = {smlt::Color::black()});
    void clear_views();

    // An overlay over `viewport`, drawn on top of all views, alpha-blended.
    // Overlays are drawn by increasing `order` (equal orders: in the order they were added),
    // and within an overlay, each quad over the ones before it -- so a 2D game can layer
    // map, sprites, labels and HUD with overlays alone.
    // The renderer owns it; it stays valid until remove_overlay() or the renderer goes away.
    Overlay* add_overlay(const smlt::Viewport& viewport, smlt::TexturePtr texture, int order = 0);
    void remove_overlay(Overlay* overlay);
    void set_overlay_texture(Overlay* overlay, smlt::TexturePtr texture);

    // width / height of a viewport in pixels, for Camera3D::set_perspective_projection()
    float aspect_ratio(const smlt::Viewport& viewport) const;

    // Dreamcast: triangles sent to the GPU and time spent drawing, last frame (0 on Linux).
    // draw_us() is the views; overlay_us() the overlays (quads submitted: overlay_quads()).
    uint32_t triangles() const;
    uint32_t draw_us() const;
    uint32_t overlay_us() const;
    uint32_t overlay_quads() const;

    struct Impl;   // platform part (renderer.cpp on Linux, renderer_dc.cpp on Dreamcast)

private:
    smlt::Scene* scene_;
    std::vector<std::unique_ptr<Overlay>> overlays_;
    std::unique_ptr<Impl> impl_;

    void pixels(const smlt::Viewport& vp, float& x, float& y, float& w, float& h) const;
    void platform_add_view(smlt::StageNode* world, smlt::Camera3D* camera,
                           const smlt::Viewport& viewport, const std::vector<smlt::Color>& background);
    void platform_add_overlay(Overlay* overlay, const smlt::Viewport& viewport, smlt::TexturePtr texture);
    void platform_remove_overlay(Overlay* overlay);
    void platform_set_overlay_texture(Overlay* overlay, smlt::TexturePtr texture);
};

} // namespace dbcs

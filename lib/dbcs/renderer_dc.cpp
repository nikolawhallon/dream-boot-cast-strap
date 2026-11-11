// dbcs::Renderer on the Dreamcast: draws views and overlays straight to the PowerVR.
// See renderer.h for the API and the kit README ("Going around GLdc") for the background.
#ifdef __DREAMCAST__

#include "renderer.h"

#include <cmath>
#include <cstring>
#include <unordered_map>

#include <arch/timer.h>
#include <dc/matrix.h>
#include <dc/pvr.h>

#include <GL/gl.h>

using namespace smlt;

// --- GLdc internals ------------------------------------------------------------------------
// Textures are still loaded by Simulant, which uploads them to video memory through GLdc. We
// draw with those same copies. GLdc keeps each one in a TextureObject
// (<kit>/sdk/simulant/deps/libgl/GL/private.h); these are its first fields, all we read.
// The static_asserts catch a layout change if the kit's pinned Simulant version moves.
struct GldcTexture {
    GLuint index;
    GLuint color;          // the PowerVR texture format bits
    GLenum min_filter;
    GLenum mag_filter;
    uint8_t* data;         // video memory address
    void* palette;
    GLushort width;
    GLushort height;
    GLushort mipmap;
    GLushort shared_bank;
    GLuint data_stride;
    GLubyte mipmap_bias, env, mipmap_count, uv_wrap;
    GLuint base_data_offset;   // where mipmap level 0 starts, if mipmaps were generated
};
static_assert(offsetof(GldcTexture, data) == 16, "GLdc TextureObject layout changed");
static_assert(offsetof(GldcTexture, width) == 24, "GLdc TextureObject layout changed");
static_assert(offsetof(GldcTexture, base_data_offset) == 40, "GLdc TextureObject layout changed");

extern "C" GldcTexture* _glGetBoundTexture();

namespace dbcs {

// --- Platform data -------------------------------------------------------------------------

struct Renderer::Impl {
    struct View {
        StageNode* world;
        Camera3D* camera;
        float x, y, w, h;         // screen pixels, origin top-left
        std::vector<uint32_t> background;   // ARGB, bottom to top
    };

    struct Part {                 // one submesh: one texture, one PowerVR header
        uint8_t header[32];
        // Groups of 4 indices. [a, b, c, d] is a triangle strip of two triangles (a, b, c) and
        // (c, b, d): the two halves of a quad, sent as 4 vertices instead of 6. If d is
        // NO_INDEX it's a single triangle (a, b, c).
        std::vector<uint16_t> strips;
        bool cull_back;
    };

    struct Mesh {
        std::vector<float> xyzuv;  // 5 floats per vertex, duplicates merged
        std::vector<Part> parts;
    };

    struct Item {                  // an actor to draw this frame
        const Mesh* mesh;
        alignas(8) float model[16];   // world matrix, column-major (matrix_t layout)
    };

    std::vector<View> views;
    struct OverlayEntry {
        Overlay* overlay;
        TexturePtr texture;
        bool compiled = false;    // header built on first draw (textures upload lazily)
        uint8_t header[32] = {};
    };
    std::vector<OverlayEntry> overlays;       // in draw order
    std::unordered_map<uint32_t, Mesh> meshes;   // by mesh asset id

    std::vector<Item> items;                  // scratch
    StageNode* items_world = nullptr;         // which world `items` was gathered from
    std::vector<float> clip;                  // transformed vertices: x, y, z, w (scratch)
    std::vector<float> screen;                // projected vertices: x, y, 1/w (scratch)
    std::vector<uint8_t> codes;               // outcodes per vertex (scratch)

    uint32_t triangles = 0;
    uint32_t draw_us = 0;

    const Mesh& mesh_for(const MeshPtr& source);
    void gather(StageNode* world);
    void draw_views();
    void draw_background(const View& view);
    void draw_mesh(const Mesh& mesh, const View& view);
    void draw_overlays();
};

static Renderer::Impl* g_active = nullptr;   // the renderer the frame hook draws

// --- Helpers -------------------------------------------------------------------------------

namespace {

constexpr uint16_t NO_INDEX = 0xffff;
pvr_dr_state_t dr_state;

// A PowerVR polygon header for a Simulant texture (untextured if null or not uploaded yet)
void compile_header(uint8_t* out, pvr_list_t list, const TexturePtr& texture) {
    pvr_poly_cxt_t cxt;
    GldcTexture* tex = nullptr;
    if(texture) {
        glBindTexture(GL_TEXTURE_2D, texture->_renderer_specific_id());
        tex = _glGetBoundTexture();
    }
    if(tex && tex->data) {
        // Same base address rule as GLdc when mipmapping is off (GL/state.c)
        pvr_poly_cxt_txr(&cxt, list, tex->color, tex->width, tex->height,
                         (pvr_ptr_t) (tex->data + tex->base_data_offset), PVR_FILTER_NEAREST);
    } else {
        pvr_poly_cxt_col(&cxt, list);
    }
    cxt.gen.culling = PVR_CULLING_NONE;            // back faces are culled on the CPU
    if(list == PVR_LIST_OP_POLY) {
        cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;    // depth is 1/w: nearer = larger
    } else {
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;    // overlays: always on top
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    }
    pvr_poly_compile((pvr_poly_hdr_t*) out, &cxt);
}

struct ClipVertex {            // clip space, with texture coordinates
    float x, y, z, w, u, v;
};

struct ScreenVertex {          // what the PowerVR gets: pixels, 1/w, texture coordinates
    float x, y, z, u, v;
};

enum : uint8_t {
    OUT_LEFT = 1, OUT_RIGHT = 2, OUT_BOTTOM = 4, OUT_TOP = 8, OUT_NEAR = 16, OUT_FAR = 32,
};

// Which sides of the view volume a clip-space point is outside of
inline uint8_t outcode(float x, float y, float z, float w) {
    uint8_t c = 0;
    if(x < -w) c |= OUT_LEFT;
    if(x > w) c |= OUT_RIGHT;
    if(y < -w) c |= OUT_BOTTOM;
    if(y > w) c |= OUT_TOP;
    if(z < -w) c |= OUT_NEAR;
    if(z > w) c |= OUT_FAR;
    return c;
}

// Signed distance to a clip plane (>= 0 = inside)
inline float plane_distance(const ClipVertex& p, uint8_t plane) {
    switch(plane) {
        case OUT_LEFT: return p.x + p.w;
        case OUT_RIGHT: return p.w - p.x;
        case OUT_BOTTOM: return p.y + p.w;
        case OUT_TOP: return p.w - p.y;
        default: return p.z + p.w;   // OUT_NEAR
    }
}

// Sutherland-Hodgman: clip a convex polygon against one plane. Returns the new vertex count.
int clip_polygon(const ClipVertex* in, int n, ClipVertex* out, uint8_t plane) {
    int count = 0;
    for(int i = 0; i < n; ++i) {
        const ClipVertex& a = in[i];
        const ClipVertex& b = in[i + 1 < n ? i + 1 : 0];
        float da = plane_distance(a, plane);
        float db = plane_distance(b, plane);
        if(da >= 0) out[count++] = a;
        if((da >= 0) != (db >= 0)) {
            float t = da / (da - db);
            ClipVertex& c = out[count++];
            c.x = a.x + (b.x - a.x) * t;
            c.y = a.y + (b.y - a.y) * t;
            c.z = a.z + (b.z - a.z) * t;
            c.w = a.w + (b.w - a.w) * t;
            c.u = a.u + (b.u - a.u) * t;
            c.v = a.v + (b.v - a.v) * t;
        }
    }
    return count;
}

// Writes one vertex to the PowerVR through the store queues. `last` ends the strip.
inline void submit_vertex(const ScreenVertex& s, bool last, uint32_t argb = 0xffffffff) {
    auto vert = (pvr_vertex_t*) pvr_dr_target(dr_state);
    vert->flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    vert->x = s.x;
    vert->y = s.y;
    vert->z = s.z;
    vert->u = s.u;
    vert->v = s.v;
    vert->argb = argb;
    vert->oargb = 0;
    pvr_dr_commit(vert);
}

inline void submit_header(const uint8_t* header) {
    auto dst = (pvr_poly_hdr_t*) pvr_dr_target(dr_state);
    std::memcpy(dst, header, sizeof(pvr_poly_hdr_t));
    pvr_dr_commit(dst);
}

// Front faces are counter-clockwise in GL terms; the screen's y axis points down
inline bool back_facing(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c) {
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) >= 0.0f;
}

} // namespace

// --- The hook into GLdc's frame ------------------------------------------------------------
// Simulant calls GLdc's glKosSwapBuffers() every frame, which builds the PowerVR frame by
// calling KallistiOS:
//   pvr_wait_ready(); pvr_scene_begin();
//   pvr_list_begin(OP) ... pvr_list_finish();   opaque, only if GLdc has opaque polygons
//   pvr_list_begin(PT) ... pvr_list_finish();   punch-through, likewise
//   pvr_list_begin(TR) ... pvr_list_finish();   translucent, likewise
//   pvr_scene_finish();
// dbcs.cmake links the game with -Wl,--wrap=<function> for pvr_scene_begin, pvr_list_begin and
// pvr_scene_finish: GLdc's calls land in the __wrap_ functions below, which call the real
// function (__real_...) and add our polygons: views to the opaque list, overlays to the
// translucent list.
//
// Each list type may be opened only ONCE per frame: KallistiOS refuses a second open and prints
// a warning to the serial console every frame (printing is slow: ~8.5 ms per frame in testing).
// So when GLdc opens a list we need, we draw into it; lists GLdc skips (all of them when
// Simulant draws nothing) we open ourselves, in order.
//
// Why wrap KallistiOS and not GLdc: Simulant (with GLdc inside) is built with link-time
// optimisation, so calls between GLdc's own functions never reach the linker as symbols and
// --wrap can't see them. A call from GLdc into KallistiOS can be redirected.

extern "C" void __real_pvr_scene_begin(void);
extern "C" int __real_pvr_list_begin(pvr_list_t list);
extern "C" int __real_pvr_scene_finish(void);

namespace {

bool views_drawn = false;      // opaque list done this frame
bool overlays_drawn = false;   // translucent list done this frame

void draw_views() {
    if(g_active) g_active->draw_views();
    views_drawn = true;
}

void draw_overlays() {
    if(g_active) g_active->draw_overlays();
    overlays_drawn = true;
}

// Our own list, for a list type GLdc didn't open
void own_list(pvr_list_t list, void (*draw)()) {
    __real_pvr_list_begin(list);
    draw();
    pvr_list_finish();   // not wrapped: the real KallistiOS function
}

} // namespace

extern "C" void __wrap_pvr_scene_begin(void) {
    __real_pvr_scene_begin();
    views_drawn = overlays_drawn = false;
}

extern "C" int __wrap_pvr_list_begin(pvr_list_t list) {
    // GLdc opens lists in the order OP, PT, TR: if it skipped the opaque one, ours goes first
    if(!views_drawn && list != PVR_LIST_OP_POLY) own_list(PVR_LIST_OP_POLY, draw_views);

    int r = __real_pvr_list_begin(list);
    if(list == PVR_LIST_OP_POLY && !views_drawn) draw_views();          // GLdc's polygons follow
    if(list == PVR_LIST_TR_POLY && !overlays_drawn) draw_overlays();
    return r;
}

extern "C" int __wrap_pvr_scene_finish(void) {
    if(!views_drawn) own_list(PVR_LIST_OP_POLY, draw_views);
    if(!overlays_drawn) own_list(PVR_LIST_TR_POLY, draw_overlays);
    return __real_pvr_scene_finish();
}

// --- Renderer ------------------------------------------------------------------------------

bool Renderer::direct() { return true; }

Renderer::Renderer(Scene* scene) : scene_(scene), impl_(new Impl()) {
    if(g_active) S_WARN("dbcs::Renderer: only one renderer draws at a time; the newest wins");
    g_active = impl_.get();
}

Renderer::~Renderer() {
    if(g_active == impl_.get()) g_active = nullptr;
}

void Renderer::platform_add_view(StageNode* world, Camera3D* camera, const Viewport& viewport,
                                 const std::vector<Color>& background) {
    Impl::View v;
    v.world = world;
    v.camera = camera;
    pixels(viewport, v.x, v.y, v.w, v.h);
    for(const Color& c: background) v.background.push_back(c.to_argb_8888());
    impl_->views.push_back(v);
}

void Renderer::clear_views() {
    impl_->views.clear();
}

void Renderer::platform_add_overlay(Overlay* o, const Viewport&, TexturePtr texture) {
    Impl::OverlayEntry entry;
    entry.overlay = o;
    entry.texture = texture;
    impl_->overlays.push_back(entry);
}

void Renderer::platform_remove_overlay(Overlay* o) {
    auto& list = impl_->overlays;
    for(size_t i = 0; i < list.size(); ++i) {
        if(list[i].overlay == o) { list.erase(list.begin() + i); return; }
    }
}

void Overlay::commit() {
    // Nothing to do: the quads are read straight from the overlay every frame
}

uint32_t Renderer::triangles() const { return impl_->triangles; }
uint32_t Renderer::draw_us() const { return impl_->draw_us; }

// --- Mesh conversion (once per mesh) -------------------------------------------------------

const Renderer::Impl::Mesh& Renderer::Impl::mesh_for(const MeshPtr& source) {
    auto it = meshes.find(source->id());
    if(it != meshes.end()) return it->second;

    Mesh& mesh = meshes[source->id()];

    // Merge identical vertices (same position and UV). Simulant's OBJ loader stores every
    // triangle corner separately; sharing them means each is transformed once per draw.
    auto vdata = source->vertex_data.get();
    std::vector<uint16_t> remap(vdata->count());
    for(uint32_t i = 0; i < vdata->count(); ++i) {
        const Vec3* p = vdata->position_at<Vec3>(i);
        const Vec2* t = vdata->texcoord0_at<Vec2>(i);
        const float v[5] = {p->x, p->y, p->z, t ? t->x : 0.0f, t ? t->y : 0.0f};
        uint32_t count = uint32_t(mesh.xyzuv.size() / 5);
        uint32_t found = count;
        for(uint32_t j = 0; j < count; ++j) {
            if(std::memcmp(&mesh.xyzuv[j * 5], v, sizeof(v)) == 0) { found = j; break; }
        }
        if(found == count) mesh.xyzuv.insert(mesh.xyzuv.end(), v, v + 5);
        remap[i] = uint16_t(found);
    }

    for(auto& submesh: source->each_submesh()) {
        if(submesh->arrangement() != MESH_ARRANGEMENT_TRIANGLES) {
            S_WARN("dbcs::Renderer: skipping a non-triangle submesh in mesh {0}", source->id());
            continue;
        }
        std::vector<uint16_t> tris;
        if(submesh->type() == SUBMESH_TYPE_INDEXED) {
            for(uint32_t i: submesh->index_data->all()) tris.push_back(remap[i]);
        } else {
            for(std::size_t r = 0; r < submesh->vertex_range_count(); ++r) {
                const VertexRange& range = submesh->vertex_ranges()[r];
                for(uint32_t i = 0; i < range.count; ++i) tris.push_back(remap[range.start + i]);
            }
        }

        // Pair consecutive triangles that share an edge into 4-vertex strips. A strip
        // [p, q, r, s] draws (p, q, r) and then (r, q, s) (the PowerVR flips every second
        // triangle of a strip to keep the winding). Quads come out of an OBJ as two
        // consecutive triangles, so this catches nearly all of them.
        Part part;
        std::size_t t = 0;
        while(t + 2 < tris.size()) {
            const uint16_t A[3] = {tris[t], tris[t + 1], tris[t + 2]};
            bool paired = false;
            if(t + 5 < tris.size()) {
                const uint16_t B[3] = {tris[t + 3], tris[t + 4], tris[t + 5]};
                for(int rot = 0; rot < 3 && !paired; ++rot) {
                    uint16_t p = A[rot], q = A[(rot + 1) % 3], r = A[(rot + 2) % 3];
                    for(int k = 0; k < 3; ++k) {          // B must be (r, q, s), rotated
                        if(B[k] == r && B[(k + 1) % 3] == q) {
                            part.strips.insert(part.strips.end(), {p, q, r, B[(k + 2) % 3]});
                            paired = true;
                            break;
                        }
                    }
                }
            }
            if(!paired) part.strips.insert(part.strips.end(), {A[0], A[1], A[2], NO_INDEX});
            t += paired ? 6 : 3;
        }

        auto material = submesh->material();
        part.cull_back = material->cull_mode() == CULL_MODE_BACK_FACE;
        compile_header(part.header, PVR_LIST_OP_POLY, material->base_color_map());
        mesh.parts.push_back(std::move(part));
    }
    return mesh;
}

// --- Per frame: views (opaque list) --------------------------------------------------------

// The visible actors directly under `world`, with their world matrices
void Renderer::Impl::gather(StageNode* world) {
    items.clear();
    items_world = world;
    if(!world) return;
    for(auto& node: world->each_child()) {
        if(node.node_type() != STAGE_NODE_TYPE_ACTOR || !node.is_visible() || node.is_destroyed()) continue;
        auto actor = static_cast<Actor*>(&node);
        auto& source = actor->base_mesh();
        if(!source) continue;
        items.emplace_back();
        items.back().mesh = &mesh_for(source);
        std::memcpy(items.back().model, actor->transform->world_space_matrix().data(), sizeof(matrix_t));
    }
}

void Renderer::Impl::draw_views() {
    uint64_t start = timer_us_gettime64();
    triangles = 0;
    items_world = nullptr;

    pvr_dr_init(&dr_state);
    for(const View& view: views) {
        draw_background(view);

        // Views of the same world share one gathering (e.g. split screen)
        if(view.world != items_world || items_world == nullptr) gather(view.world);

        // proj * view, then per actor (* model) on the SH-4's matrix unit
        Mat4 pv = view.camera->projection_matrix() * view.camera->view_matrix();
        alignas(8) matrix_t pv_matrix;
        std::memcpy(pv_matrix, pv.data(), sizeof(matrix_t));

        for(const Item& item: items) {
            mat_load(&pv_matrix);
            mat_apply((matrix_t*) item.model);   // XMTRX = proj * view * model
            draw_mesh(*item.mesh, view);
        }
    }
    draw_us = uint32_t(timer_us_gettime64() - start);
}

void Renderer::Impl::draw_background(const View& view) {
    static uint8_t header[32];
    static bool compiled = false;
    if(!compiled) {
        compile_header(header, PVR_LIST_OP_POLY, nullptr);   // untextured, Gouraud-shaded
        compiled = true;
    }
    submit_header(header);

    // One strip from the bottom of the view to the top, as far away as possible
    const int count = int(view.background.size());
    const int rows = count > 1 ? count - 1 : 1;
    for(int r = 0; r <= rows; ++r) {
        float y = view.y + view.h * (1.0f - float(r) / rows);
        uint32_t argb = view.background[r < count ? r : count - 1];
        submit_vertex({view.x, y, 0.00001f, 0, 0}, false, argb);
        submit_vertex({view.x + view.w, y, 0.00001f, 0, 0}, r == rows, argb);
    }
}

void Renderer::Impl::draw_mesh(const Mesh& mesh, const View& view) {
    const float hw = view.w * 0.5f, hh = view.h * 0.5f;
    const float cx = view.x + hw, cy = view.y + hh;

    // 1. Transform every vertex (XMTRX is loaded), classify it against the view volume, and
    //    project it to the screen if it's in front of the near plane
    const uint32_t count = uint32_t(mesh.xyzuv.size() / 5);
    clip.resize(count * 4);
    screen.resize(count * 3);
    codes.resize(count);
    const float* src = mesh.xyzuv.data();
    float* dst = clip.data();
    float* scr = screen.data();
    uint8_t all_and = 0xff;
    for(uint32_t i = 0; i < count; ++i, src += 5, dst += 4, scr += 3) {
        float x = src[0], y = src[1], z = src[2], w = 1.0f;
        mat_trans_nodiv(x, y, z, w);
        dst[0] = x; dst[1] = y; dst[2] = z; dst[3] = w;
        uint8_t code = outcode(x, y, z, w);
        codes[i] = code;
        all_and &= code;
        if(!(code & OUT_NEAR)) {
            // 1/w via the SH-4's fast reciprocal square root (fsrra); w > 0 here
            float inv_w = 1.0f / std::sqrt(w * w);
            scr[0] = cx + x * inv_w * hw;
            scr[1] = cy - y * inv_w * hh;
            scr[2] = inv_w;
        }
    }
    if(all_and) return;   // the whole mesh is outside this view

    auto projected = [&](uint16_t i) {
        const float* s = &screen[i * 3];
        const float* t = &mesh.xyzuv[i * 5 + 3];
        return ScreenVertex{s[0], s[1], s[2], t[0], t[1]};
    };
    auto clip_vertex = [&](uint16_t i) {
        const float* c = &clip[i * 4];
        const float* t = &mesh.xyzuv[i * 5 + 3];
        return ClipVertex{c[0], c[1], c[2], c[3], t[0], t[1]};
    };

    // 2. Submit the triangles
    for(const Part& part: mesh.parts) {
        // One triangle, any case: rejected, drawn as is, or clipped first
        auto draw_triangle = [&](uint16_t i0, uint16_t i1, uint16_t i2) {
            uint8_t c0 = codes[i0], c1 = codes[i1], c2 = codes[i2];
            if(c0 & c1 & c2) return;              // entirely outside one side of the view

            if(!(c0 | c1 | c2)) {                 // entirely inside
                ScreenVertex s0 = projected(i0), s1 = projected(i1), s2 = projected(i2);
                if(part.cull_back && back_facing(s0, s1, s2)) return;
                submit_vertex(s0, false);
                submit_vertex(s1, false);
                submit_vertex(s2, true);
                triangles++;
                return;
            }

            // Crosses the near plane or a view edge: clip, then draw the result as a fan.
            // Clipping to the view's edges keeps split-screen views from drawing into each other.
            ClipVertex a[9], b[9];
            a[0] = clip_vertex(i0);
            a[1] = clip_vertex(i1);
            a[2] = clip_vertex(i2);
            int m = 3;
            uint8_t straddled = (c0 | c1 | c2) & ~OUT_FAR;
            ClipVertex* in = a;
            ClipVertex* out = b;
            for(uint8_t plane = OUT_LEFT; plane <= OUT_NEAR && m >= 3; plane <<= 1) {
                if(!(straddled & plane)) continue;
                m = clip_polygon(in, m, out, plane);
                std::swap(in, out);
            }
            if(m < 3) return;

            ScreenVertex s[9];
            for(int k = 0; k < m; ++k) {
                float inv_w = 1.0f / std::sqrt(in[k].w * in[k].w);
                s[k] = {cx + in[k].x * inv_w * hw, cy - in[k].y * inv_w * hh, inv_w, in[k].u, in[k].v};
            }
            if(part.cull_back && back_facing(s[0], s[1], s[2])) return;
            for(int k = 1; k + 1 < m; ++k) {
                submit_vertex(s[0], false);
                submit_vertex(s[k], false);
                submit_vertex(s[k + 1], true);
                triangles++;
            }
        };

        submit_header(part.header);
        const uint16_t* st = part.strips.data();
        for(std::size_t g = 0; g + 3 < part.strips.size(); g += 4) {
            uint16_t p = st[g], q = st[g + 1], r = st[g + 2], s = st[g + 3];
            if(s == NO_INDEX) {
                draw_triangle(p, q, r);
                continue;
            }
            // The common case: both halves on screen and facing us -> one 4-vertex strip
            if(!(codes[p] | codes[q] | codes[r] | codes[s])) {
                ScreenVertex sp = projected(p), sq = projected(q), sr = projected(r), ss = projected(s);
                bool front = !part.cull_back || (!back_facing(sp, sq, sr) && !back_facing(sr, sq, ss));
                if(front) {
                    submit_vertex(sp, false);
                    submit_vertex(sq, false);
                    submit_vertex(sr, false);
                    submit_vertex(ss, true);
                    triangles += 2;
                    continue;
                }
            }
            draw_triangle(p, q, r);
            draw_triangle(r, q, s);
        }
    }
}

// --- Per frame: overlays (translucent list) ------------------------------------------------

void Renderer::Impl::draw_overlays() {
    pvr_dr_init(&dr_state);
    for(OverlayEntry& e: overlays) {
        if(!e.compiled) {
            compile_header(e.header, PVR_LIST_TR_POLY, e.texture);
            e.compiled = true;
        }
        submit_header(e.header);
        const Overlay& o = *e.overlay;
        for(const Quad& q: o.quads()) {
            // Overlay pixels are y-up from the viewport's bottom; the screen is y-down
            float left = o.screen_x() + q.x, right = left + q.w;
            float top = o.screen_y() + o.height() - (q.y + q.h), bottom = top + q.h;
            submit_vertex({left, top, 1.0f, q.u0, q.v1}, false);
            submit_vertex({right, top, 1.0f, q.u1, q.v1}, false);
            submit_vertex({left, bottom, 1.0f, q.u0, q.v0}, false);
            submit_vertex({right, bottom, 1.0f, q.u1, q.v0}, true);
        }
    }
}

} // namespace dbcs

#endif

// Entry point. Identical on Linux and Dreamcast: Simulant hides the platform
// (SDL2 window on Linux, KallistiOS + GLdc on the Dreamcast).
#include <simulant/simulant.h>

#include "game_scene.h"

using namespace smlt;

// The Application owns the window, the main loop, input, audio and the scene manager.
// init() runs once at startup: register your scenes there. The scene called "main"
// is activated first.
class DreamGame : public Application {
public:
    DreamGame(const AppConfig& config) :
        Application(config) {}

    bool init() override {
        scenes->register_scene<GameScene>("main");
        return true;
    }
};

int main(int argc, char* argv[]) {
    _S_UNUSED(argc);
    _S_UNUSED(argv);

    AppConfig config;
    config.title = "DreamGame";

    // The Dreamcast outputs 640x480; use the same on PC so both look alike.
    config.width = 640;
    config.height = 480;
    config.fullscreen = false;

    // Use the fixed-function GL1 renderer everywhere. That is what the Dreamcast uses (via GLdc),
    // so the PC build shows what the console will show. (Simulant's default PC renderer,
    // "gl2x", uses PBR shaders that render untextured colored materials black.)
    config.development.force_renderer = "gl1x";

    // Where to look for files passed to load_texture() / load_mesh() etc.
    config.search_paths.push_back("assets");                       // next to the binary (PC)
    config.search_paths.push_back(kfs::exe_dirname() + "/assets");
    config.search_paths.push_back("/cd/assets");                   // on the Dreamcast disc

#ifndef NDEBUG
    config.log_level = LOG_LEVEL_DEBUG;   // Debug builds: verbose engine log (stdout / DC serial)
#endif

    DreamGame app(config);
    return app.run();   // main loop; returns after app->stop_running()
}

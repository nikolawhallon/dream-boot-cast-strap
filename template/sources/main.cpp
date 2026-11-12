#include <simulant/simulant.h>

#include "game_scene.h"

using namespace smlt;

class DreamGame : public Application {
public:
    DreamGame(const AppConfig& config) :
        Application(config) {}

    bool init() override {
        scenes->register_scene<GameScene>("main");   // "main" starts first
        return true;
    }
};

int main(int argc, char* argv[]) {
    _S_UNUSED(argc);
    _S_UNUSED(argv);

    AppConfig config;
    config.title = "DreamGame";
    config.width = 640;    // the Dreamcast's resolution
    config.height = 480;
    config.fullscreen = false;

    // The Dreamcast's fixed-function renderer, on PC too (gl2x draws untextured materials black)
    config.development.force_renderer = "gl1x";

    config.search_paths.push_back("assets");
    config.search_paths.push_back(kfs::exe_dirname() + "/assets");
    config.search_paths.push_back("/cd/assets");   // Dreamcast disc

#ifndef NDEBUG
    config.log_level = LOG_LEVEL_DEBUG;
#endif

    DreamGame app(config);
    return app.run();
}

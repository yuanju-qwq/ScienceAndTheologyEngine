// SNT engine main entry.
// P2.B1: reduced to Engine class init/run/shutdown. All subsystem setup
// + the per-frame loop live in engine/engine.cpp (the Engine module).
// P2.E: loads game/config/engine.json before init; falls back to defaults
// if the file is missing so the engine always runs out-of-the-box.

#define SNT_LOG_CHANNEL "app"
#include "core/log.h"

#include "core/engine_config.h"
#include "core/path_utils.h"
#include "engine/engine.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    // Locate the project root BEFORE loading config, so game/config/engine.json
    // resolves correctly regardless of the launcher's CWD (VS, explorer,
    // packaged install). init() is idempotent; Engine::init also calls it.
    snt::core::path_utils::init();

    // Load engine config from JSON. path_utils::resolve makes the path
    // absolute; missing file is non-fatal — defaults are returned; only a
    // JSON parse error aborts startup.
    const std::string config_path =
        snt::core::path_utils::resolve("game/config/engine.json");
    auto config_result = snt::core::load_engine_config(config_path);
    if (!config_result) {
        SNT_LOG_ERROR("Config load failed: %s",
                      config_result.error().format().c_str());
        return 1;
    }

    snt::engine::Engine engine;
    if (auto r = engine.init(*config_result); !r) {
        SNT_LOG_ERROR("Engine init failed: %s", r.error().format().c_str());
        return 1;
    }
    engine.run();
    engine.shutdown();
    return 0;
}

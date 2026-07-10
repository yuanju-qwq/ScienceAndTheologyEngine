// Runtime path contract shared by the engine and its host application.
//
// The engine never discovers a parent repository or assumes a directory name.
// A host supplies the three roots once during Engine::init:
//   - engine_root: engine-owned packaged resources (shaders, ICU data, ...)
//   - game_root: game-owned content (config, scenes, scripts, assets, ...)
//   - user_root: writable per-install data (logs, saves, caches, ...)
//
// Keeping this boundary in core makes the engine independently buildable and
// lets an embedding game choose any package layout or submodule location.

#pragma once

#include "core/expected.h"

#include <string>
#include <string_view>

namespace snt::core {

struct RuntimePaths {
    std::string engine_root;
    std::string game_root;
    std::string user_root;
};

namespace path_utils {

// Normalizes and installs the host-owned path contract. Reconfiguration is
// intentional so integration tests and editor sessions can start cleanly.
Expected<void> configure(RuntimePaths paths);

// Returns whether configure() completed successfully for this process.
bool configured();

// Read-only access to the normalized roots. Empty until configure() succeeds.
const RuntimePaths& runtime_paths();

// Resolve a relative resource path under its declared ownership root. Absolute
// paths pass through unchanged, which permits hosts to explicitly opt out of
// packaged-relative resolution for a particular resource.
std::string resolve_engine(std::string_view relative_path);
std::string resolve_game(std::string_view relative_path);
std::string resolve_user(std::string_view relative_path);

// Join two path segments with the platform-native separator. Does not make a
// path absolute and is safe for composing host paths before configure().
std::string join(std::string_view a, std::string_view b);

// Check whether a file or directory exists without throwing.
bool exists(std::string_view path);

}  // namespace path_utils
}  // namespace snt::core

// Runtime path contract shared by the engine and its host application.
//
// The engine never discovers a parent repository or assumes a directory name.
// A host supplies the three roots once during Runtime::init:
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

// Immutable resolver for one Runtime's host-owned path contract.
//
// Ownership and lifetime: Runtime creates one resolver during init and owns it
// until shutdown. Subsystems receive a borrowed const reference and must not
// retain it beyond their owning Runtime. The value has no process-global state,
// so independent runtimes and tests may resolve different package layouts.
//
// Thread affinity: resolving paths only reads normalized strings and is safe on
// any thread. Hosts must finish construction before publishing it to workers.
class RuntimePathResolver {
public:
    // Validates and normalizes the three package roots supplied by the host.
    // Each root must be non-empty; it does not need to exist yet.
    [[nodiscard]] static Expected<RuntimePathResolver> create(RuntimePaths paths);

    const RuntimePaths& roots() const noexcept;

    // Resolve a relative resource path under its declared ownership root.
    // Absolute paths pass through unchanged, allowing a host to explicitly
    // opt out of packaged-relative resolution for one resource.
    std::string resolve_engine(std::string_view relative_path) const;
    std::string resolve_game(std::string_view relative_path) const;
    std::string resolve_user(std::string_view relative_path) const;

private:
    explicit RuntimePathResolver(RuntimePaths paths);

    RuntimePaths paths_;
};

namespace path_utils {

// Join two path segments with the platform-native separator. Does not make a
// path absolute and is safe for composing host paths before resolver creation.
std::string join(std::string_view a, std::string_view b);

// Check whether a file or directory exists without throwing.
bool exists(std::string_view path);

}  // namespace path_utils
}  // namespace snt::core

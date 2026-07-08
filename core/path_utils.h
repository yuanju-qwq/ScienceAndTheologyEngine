// Path utilities: resolve engine resources independent of CWD.
//
// Design goals:
//   - Remove the dependency on the process's current working directory.
//     Running the .exe from VS, from the bin/ folder, or from a packaged
//     install should all resolve "shaders/mesh.vert.spv" the same way.
//   - Anchor all paths to the project root, located by walking up from the
//     executable's directory until a marker file/dir is found (currently
//     `game/config/engine.json` or the `snt_engine/` source dir during dev).
//   - Cross-platform: Windows uses GetModuleFileNameW; Linux reads
//     /proc/self/exe; macOS uses _NSGetExecutablePath (future).
//   - Thin wrappers over std::filesystem; no virtual file system, no
//     archive mounts — that's the P3 resource system's job.
//
// Resource routing (by path prefix):
//   - "shaders/..." or "builtin/..."  -> engine subdir (snt_engine/)
//   - "game/...", "config/...", "scenes/...", "test_assets/..."
//                                      -> project root (game content / tests)
//   - fallback                         -> project root (back-compat)
//
// Usage:
//   // Once at startup (main.cpp):
//   snt::core::path_utils::init();
//
//   // Anywhere after:
//   std::string shader = snt::core::path_utils::resolve("shaders/mesh.vert.spv");
//   std::string config = snt::core::path_utils::resolve("game/config/engine.json");
//   if (snt::core::path_utils::exists(shader)) { ... }

#pragma once

#include <string>
#include <string_view>

namespace snt::core {
namespace path_utils {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
// Locate the project root by walking up from the executable's directory
// until a marker is found (game/config/engine.json, or the snt_engine/
// source dir in development builds). Caches the result so subsequent
// resolve() calls are O(1). Safe to call multiple times; idempotent.
//
// Returns true on success. On failure, resolve() will fall back to
// returning the input path as-is (so the engine still runs, just with
// CWD-dependent behavior).
bool init();

// ---------------------------------------------------------------------------
// Path operations
// ---------------------------------------------------------------------------
// Resolve a relative path against the project root. Always returns an
// absolute path after init() succeeds; before init() or on init failure,
// returns the input unchanged (so callers get sensible behavior).
//
// Routing by prefix (see "Resource routing" in the file header):
//   - "shaders/..." or "builtin/..."  -> project_root/engine_subdir/...
//   - other prefixes                  -> project_root/...
//
// `relative_path` may use either '/' or '\\' as separator; the result
// uses the platform-native separator.
std::string resolve(std::string_view relative_path);

// Join two path segments with the platform separator. Helper for callers
// that build paths incrementally. Does NOT make the result absolute.
std::string join(std::string_view a, std::string_view b);

// Check whether a path exists on disk (file or directory).
bool exists(std::string_view path);

// Read-only access to the cached project root (empty if init() failed
// or was never called). The project root contains both the engine
// submodule (snt_engine/) and the game content (game/).
const std::string& project_root();

// Read-only access to the engine submodule directory name relative to
// the project root (e.g. "snt_engine"). Empty if init() failed.
const std::string& engine_subdir();

}  // namespace path_utils
}  // namespace snt::core

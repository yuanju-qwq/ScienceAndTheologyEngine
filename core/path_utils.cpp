// Path utilities — implementation.

#define SNT_LOG_CHANNEL "path"
#include "core/log.h"
#include "core/path_utils.h"

#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#  include <windows.h>  // GetModuleFileNameW
#elif defined(__linux__)
#  include <unistd.h>   // readlink
#  include <limits.h>   // PATH_MAX
#endif

namespace snt::core {
namespace path_utils {

namespace {

// Cached project root (absolute, native separators). Empty until init()
// succeeds. The project root is the top-level directory containing both
// the engine submodule (snt_engine/) and the game content (game/).
std::string g_project_root;

// Engine submodule directory name relative to project root
// (e.g. "snt_engine"). Empty until init() succeeds.
std::string g_engine_subdir;

// ---------------------------------------------------------------------------
// Locate the directory containing the running executable.
// ---------------------------------------------------------------------------
// Returns an empty string on failure. The result is absolute + ends with
// the platform separator so concatenation is straightforward.
std::string executable_dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    std::filesystem::path exe_path(buf);
    std::error_code ec;
    auto dir = std::filesystem::absolute(exe_path.parent_path(), ec);
    if (ec) return {};
    std::string s = dir.string();
    if (!s.empty() && s.back() != '\\' && s.back() != '/') {
        s += "\\";
    }
    return s;
#elif defined(__linux__)
    char buf[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return {};
    buf[len] = '\0';
    std::filesystem::path exe_path(buf);
    std::error_code ec;
    auto dir = std::filesystem::absolute(exe_path.parent_path(), ec);
    if (ec) return {};
    std::string s = dir.string();
    if (!s.empty() && s.back() != '/') {
        s += "/";
    }
    return s;
#else
#  error "path_utils::executable_dir() not implemented for this platform"
#endif
}

// Walk up from `start` looking for a marker that identifies the project
// root. The project root contains both the engine submodule (snt_engine/)
// and the game content (game/). Markers (any one suffices):
//   - "game/config/engine.json"  (packaged / dev builds)
//   - "snt_engine/cmake/fetch_third_party.cmake" (dev source tree)
//
// On success, fills `out_project_root` (with trailing separator) and
// `out_engine_subdir` (e.g. "snt_engine"). Returns true on success.
//
// Why fetch_third_party.cmake instead of snt_engine/CMakeLists.txt?
//   The Engine *module* also has its own CMakeLists.txt at
//   snt_engine/engine/CMakeLists.txt. When the executable lives under
//   snt_engine/build/..., walking up reaches snt_engine/ first, where
//   snt_engine/engine/CMakeLists.txt exists — causing a false positive
//   that sets the root to snt_engine/engine/ (one level too deep).
//   fetch_third_party.cmake only exists at the engine *root*
//   (snt_engine/cmake/), never in any submodule, so it is a unique marker.
bool find_root_from(const std::string& start,
                    std::string& out_project_root,
                    std::string& out_engine_subdir) {
    std::error_code ec;
    auto dir = std::filesystem::absolute(start, ec);
    if (ec) return false;

    const std::string sep = std::string(1, std::filesystem::path::preferred_separator);

    // Bounded climb to avoid infinite loops on weird filesystems.
    for (int i = 0; i < 16; ++i) {
        const std::string s = dir.string();

        // Marker 1: packaged / dev layout — game/config/engine.json under
        // the project root.
        if (std::filesystem::exists(dir / "game" / "config" / "engine.json", ec)) {
            out_project_root = s;
            if (!out_project_root.empty() &&
                out_project_root.back() != std::filesystem::path::preferred_separator) {
                out_project_root += sep;
            }
            out_engine_subdir = "snt_engine";
            return true;
        }

        // Marker 2: dev source layout — snt_engine/cmake/fetch_third_party.cmake.
        // Unique to the project root (no submodule has a cmake/ subdir).
        if (std::filesystem::exists(dir / "snt_engine" / "cmake" / "fetch_third_party.cmake", ec)) {
            out_project_root = s;
            if (!out_project_root.empty() &&
                out_project_root.back() != std::filesystem::path::preferred_separator) {
                out_project_root += sep;
            }
            out_engine_subdir = "snt_engine";
            return true;
        }

        // Stop at filesystem root.
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool init() {
    if (!g_project_root.empty()) return true;  // already initialized

    const std::string exe_dir = executable_dir();
    if (exe_dir.empty()) {
        SNT_LOG_WARN("path_utils: could not locate executable directory; "
                     "falling back to CWD-relative paths");
        return false;
    }

    std::string root;
    std::string engine_subdir;
    if (!find_root_from(exe_dir, root, engine_subdir)) {
        SNT_LOG_WARN("path_utils: could not locate project root from '%s'; "
                     "falling back to CWD-relative paths",
                     exe_dir.c_str());
        return false;
    }

    g_project_root = root;
    g_engine_subdir = engine_subdir;
    SNT_LOG_INFO("path_utils: project root = %s (engine subdir = %s)",
                 g_project_root.c_str(), g_engine_subdir.c_str());
    return true;
}

// Resolve a relative path to an absolute path based on resource category.
//
// Routing rules (by path prefix):
//   - "shaders/..." or "builtin/..."  -> engine subdir (snt_engine/)
//   - "game/..." or "config/..." or "scenes/..." or "test_assets/..."
//                                      -> project root (game content / tests)
//   - fallback                         -> project root (back-compat)
//
// This separates engine built-in resources (shaders, default materials)
// from game content (scenes, configs, test assets). The engine never
// writes to game/ — it only reads from it.
std::string resolve(std::string_view relative_path) {
    if (g_project_root.empty()) {
        // init() not called or failed — return input as-is so the engine
        // still runs (with CWD-dependent behavior).
        return std::string(relative_path);
    }
    // Replace forward slashes with native separator for cross-platform
    // config files (which always use '/').
    std::string p(relative_path);
    for (char& c : p) {
        if (c == '/') c = std::filesystem::path::preferred_separator;
    }

    // Route by prefix: engine built-in resources vs game content.
    const std::string& engine = g_engine_subdir;
    const std::string sep(1, std::filesystem::path::preferred_separator);

    // Engine built-in resources: shaders/, builtin/
    if (p.rfind("shaders", 0) == 0 || p.rfind("builtin", 0) == 0) {
        return g_project_root + engine + sep + p;
    }
    // Game content + test assets: project root.
    // (config/, scenes/, game/, test_assets/, assets/, ...)
    return g_project_root + p;
}

std::string join(std::string_view a, std::string_view b) {
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    std::string result(a);
    if (result.back() != '/' && result.back() != '\\') {
        result += std::filesystem::path::preferred_separator;
    }
    // Strip a leading separator from b so we don't double up.
    std::string_view b_view = b;
    if (!b_view.empty() && (b_view.front() == '/' || b_view.front() == '\\')) {
        b_view.remove_prefix(1);
    }
    result.append(b_view);
    return result;
}

bool exists(std::string_view path) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(std::string(path)), ec);
}

const std::string& project_root() {
    return g_project_root;
}

const std::string& engine_subdir() {
    return g_engine_subdir;
}

}  // namespace path_utils
}  // namespace snt::core

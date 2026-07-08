// Engine-wide profiling macros.
//
// Design goals:
//   - Define a stable macro API (SNT_PROFILE_SCOPE / SNT_PROFILE_FUNCTION /
//     SNT_PROFILE_THREAD) that compiles to nothing today, so call sites can
//     be instrumented NOW without waiting for a real profiler backend.
//   - Late binding to a backend: switch backends by changing a single
//     CMake option (SNT_PROFILE_BACKEND). No call-site edits needed.
//   - Zero cost in the default (NONE) configuration: every macro expands
//     to ((void)0) and is elided by the compiler.
//
// Supported backends (selected via CMake -DSNT_PROFILE_BACKEND=...):
//   NONE   (default) — no-op. Ship with this.
//   TRACY          — Tracy Profiler. Requires tracy/Tracy.hpp on the
//                    include path (link target supplies it).
//   OPTICK         — Optick Profiler. Requires Optick.h on the include
//                    path.
//
// The CMake option translates to one of these compile definitions:
//   SNT_PROFILE_BACKEND_NONE / _TRACY / _OPTICK
// Exactly one is defined; see engine/core/CMakeLists.txt.
//
// Usage:
//   void RenderSystem::update(World& w, float dt) {
//       SNT_PROFILE_FUNCTION();
//       SNT_PROFILE_SCOPE("gather_meshes");
//       ...
//   }
//
// Header-only. No .cpp counterpart; safe to include from anywhere.

#pragma once

// ---------------------------------------------------------------------------
// Backend selection.
// ---------------------------------------------------------------------------
// Resolve the active backend. If none of the SNT_PROFILE_BACKEND_* macros
// are defined, fall back to NONE so the header is usable standalone (e.g.
// from a scratch test build that bypasses the engine CMake).
#if !defined(SNT_PROFILE_BACKEND_NONE) && \
    !defined(SNT_PROFILE_BACKEND_TRACY) && \
    !defined(SNT_PROFILE_BACKEND_OPTICK)
#  define SNT_PROFILE_BACKEND_NONE 1
#endif

// ===========================================================================
// Backend: NONE (default)
// ===========================================================================
// All macros compile to nothing. The (void)0 form keeps the macro usable
// as a statement (`if (x) SNT_PROFILE_FUNCTION();`) and avoids unused-
// variable warnings.
#if defined(SNT_PROFILE_BACKEND_NONE)

#  define SNT_PROFILE_SCOPE(name)      ((void)0)
#  define SNT_PROFILE_FUNCTION()       ((void)0)
#  define SNT_PROFILE_THREAD(name)     ((void)0)
#  define SNT_PROFILE_FRAME_MARK(name) ((void)0)

// ===========================================================================
// Backend: Tracy (https://github.com/wolfpld/tracy)
// ===========================================================================
// Maps SNT_* macros to the corresponding TracyC / TracyLined variants.
// We use the C API (TracyC.h) so call sites don't need to be C++ files,
// and we don't pull in the C++ headers here. The link target is expected
// to provide TracyClient.
#elif defined(SNT_PROFILE_BACKEND_TRACY)

#  include <tracy/TracyC.h>

// ZoneCtx ensures the zone is ended when the scope exits; combined with
// the source-location literal, this mirrors Tracy's C++ ZoneScope.
namespace snt::core::detail {
class TracyZoneCtx {
public:
    explicit TracyZoneCtx(const char* name) {
        // The static source location struct is created once per call site.
        ctx_ = ___tracy_emit_zone_begin_alloc(
            &___tracy_alloc_srcloc_name(0, name, 0, __FILE__, __LINE__, 0),
            1);
    }
    ~TracyZoneCtx() { ___tracy_emit_zone_end(&ctx_); }
    TracyZoneCtx(const TracyZoneCtx&) = delete;
    TracyZoneCtx& operator=(const TracyZoneCtx&) = delete;
private:
    TracyCZoneCtx ctx_;
};
}  // namespace snt::core::detail

#  define SNT_PROFILE_SCOPE(name)      \
      ::snt::core::detail::TracyZoneCtx snt_tracy_zone_##__LINE__(name)
#  define SNT_PROFILE_FUNCTION()       SNT_PROFILE_SCOPE(__FUNCTION__)
#  define SNT_PROFILE_THREAD(name)     TracyCSetThreadName(name)
#  define SNT_PROFILE_FRAME_MARK(name) TracyCFrameMark

// ===========================================================================
// Backend: Optick (https://github.com/bombomby/optick)
// ===========================================================================
#elif defined(SNT_PROFILE_BACKEND_OPTICK)

#  include <Optick.h>

#  define SNT_PROFILE_SCOPE(name)      OPTICK_EVENT(name)
#  define SNT_PROFILE_FUNCTION()       OPTICK_EVENT()
#  define SNT_PROFILE_THREAD(name)     OPTICK_THREAD(name)
#  define SNT_PROFILE_FRAME_MARK(name) OPTICK_TAG("FRAME", name)

// ===========================================================================
// Fallback: unknown backend.
// ===========================================================================
#else
#  error "Unknown SNT_PROFILE_BACKEND value. Expected NONE, TRACY, or OPTICK."
#endif

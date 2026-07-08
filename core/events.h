// Engine-wide event types published via the EventBus (entt::dispatcher).
//
// Design goals:
//   - Decouple producers from consumers. The Window pumps SDL events into
//     the bus; the Engine publishes state changes; subsystems subscribe to
//     what they care about. No module needs to know who else is listening.
//   - Late-binding: future subscribers (script reload, asset load, UI
//     refresh) can subscribe to existing events without touching producers.
//   - POD-style event structs: cheap to copy, no heap, no ownership.
//
// Placement note: lives in core/ (not ecs/) so the input module can
// subscribe without creating a circular dependency. ecs depends on input;
// if events.h lived in ecs, input would need to depend back on ecs. The
// EventBus alias itself stays in ecs/event_bus.h (it needs entt, which
// core does not link against).
//
// Usage:
//   // Publishing (producer side):
//   bus.enqueue<SdlEventFired>({sdl_event_ptr});
//   bus.update();  // dispatch to all subscribers
//
//   // Subscribing (consumer side):
//   bus.sink<SdlEventFired>().connect<&InputSystem::on_sdl_event>(input_sys);
//
// Thread-safety: entt::dispatcher is NOT thread-safe. Publish + subscribe
// must happen on a single thread (the main thread for now). Worker-thread
// events will need a thread-safe queue + main-thread flush; that's a P3
// concern.

#pragma once

#include <cstdint>
#include <string>

namespace snt::core {

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------
// Fired by Window for every polled SDL_Event. Carries a const void* (not
// SDL_Event*) so the events header doesn't pull in SDL. Subscribers cast
// back to SDL_Event* in their .cpp where SDL headers are available.
//
// Lifetime: `sdl_event` is valid only during dispatch — do not retain.
struct SdlEventFired {
    const void* sdl_event = nullptr;
};

// Fired by Engine when the mouse-lock state (SDL relative mode) changes.
// CameraSystem subscribes to skip mouse-look while unlocked.
struct MouseLockChanged {
    bool locked = false;
};

// ---------------------------------------------------------------------------
// Asset events (P3+ use)
// ---------------------------------------------------------------------------
// Fired by the asset system when an async load completes. UI / script
// systems subscribe to refresh their state.
struct AssetLoaded {
    std::string path;
    uint32_t    handle_id = 0xFFFFFFFFu;  // MeshHandle / TextureHandle id
};

// ---------------------------------------------------------------------------
// Script events (P3+ use)
// ---------------------------------------------------------------------------
// Fired by the script system after a hot-reload. ECS / UI subscribe to
// rebuild their script-bound components.
struct ScriptReloaded {
    std::string module_name;
};

// ---------------------------------------------------------------------------
// Config events (P3+ use)
// ---------------------------------------------------------------------------
// Fired by Engine when the config file is reloaded at runtime. All
// subsystems subscribe to re-read their slice of the config.
struct ConfigReloaded {
    std::string config_path;
};

}  // namespace snt::core

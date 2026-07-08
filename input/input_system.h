// InputSystem: consumes raw SDL events and produces an InputState snapshot.
//
// Lifecycle per frame:
//   1. new_frame()             — clears per-frame deltas (pressed, mouse_dx/dy)
//   2. process_event(evt) x N — called by Window for each SDL event
//   3. end_frame()             — finalizes state (polls SDL_GetKeyboardState
//                                for held keys to catch up with repeat/loss)
//   4. state()                 — CameraSystem etc. read the snapshot
//
// Held state is sourced from SDL_GetKeyboardState at end_frame() rather
// than tracked from events, because SDL may coalesce or drop KEY_DOWN
// events under high key repeat rates. The pressed/edge events are still
// tracked manually because SDL has no equivalent "was pressed this frame"
// query.
//
// API note: process_event takes `const void*` rather than forward-
// declaring SDL_Event, because SDL3's SDL_Event is a union typedef that
// cannot be forward-declared portably. Callers cast SDL_Event* to
// const void* at the call site.

#pragma once

#include "core/events.h"     // SdlEventFired (event bus subscription)
#include "input/input_state.h"

namespace snt::input {

class InputSystem {
public:
    InputSystem() = default;
    ~InputSystem() = default;

    // Non-copyable; the state is mutable + per-instance.
    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    // Clear per-frame state. Call at the START of a new frame, before
    // events are pumped.
    void new_frame();

    // Ingest a single SDL event. `sdl_event` must point to a valid
    // SDL_Event; the pointer is cast back inside the .cpp where SDL
    // headers are available.
    void process_event(const void* sdl_event);

    // EventBus subscriber: called by entt::dispatcher when an SdlEventFired
    // event is published. Forwards to process_event() so InputSystem no
    // longer needs Engine to call it directly — any module can publish.
    void on_sdl_event(const snt::core::SdlEventFired& evt) {
        process_event(evt.sdl_event);
    }

    // Finalize state for this frame. Called after all events are processed.
    // Refreshes held-key state from SDL_GetKeyboardState.
    void end_frame();

    // Read-only access to the current input snapshot.
    const InputState& state() const { return state_; }

private:
    InputState state_;
};

}  // namespace snt::input

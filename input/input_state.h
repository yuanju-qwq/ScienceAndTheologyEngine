// InputState: per-frame snapshot of keyboard + mouse state.
//
// Two layers of state:
//   - Held:    true while a key/button is physically held down.
//   - Pressed: true only on the frame the key/button transitioned to down.
//
// `mouse_dx` / `mouse_dy` accumulate relative motion between frames and
// are reset by InputSystem::new_frame(). This matches the common pattern
// where CameraSystem reads delta then lets the next frame clear it.
//
// Design note: arrays indexed by SDL scancode / button id for cache
// friendliness. Upper layers must not depend on SDL constants directly —
// InputSystem maps SDL events into these arrays.

#pragma once

#include <cstdint>

namespace snt::input {

// Keyboard state: indexed by SDL scancode (0..SDL_SCANCODE_COUNT-1).
// SDL3's SDL_SCANCODE_COUNT is 512; we size for that.
constexpr uint32_t kKeyCount = 512;

// Mouse button indices: 0=Left, 1=Middle, 2=Right (matches SDL_BUTTON_LMASK
// ordering after shifting). See InputSystem::process_event for the mapping.
constexpr uint32_t kMouseButtonCount = 3;

struct InputState {
    // Keyboard: held = currently down, pressed = went down this frame.
    bool key_held[kKeyCount] = {};
    bool key_pressed[kKeyCount] = {};

    // Mouse buttons.
    bool mouse_held[kMouseButtonCount] = {};
    bool mouse_pressed[kMouseButtonCount] = {};

    // Relative mouse motion accumulated since last new_frame().
    float mouse_dx = 0.0f;
    float mouse_dy = 0.0f;

    // Absolute mouse position (window-space, pixels).
    int32_t mouse_x = 0;
    int32_t mouse_y = 0;

    // True if the window requested close (SDL_EVENT_QUIT).
    // Window still owns should_close semantics; this is a mirror for
    // systems that want to react to quit without holding a Window*.
    bool quit_requested = false;

    // P2.A2 MC-style pointer lock signals.
    // `esc_pressed` is true on the frame ESC was pressed (edge event).
    // Upper layers use it to toggle relative mouse mode off.
    bool esc_pressed = false;

    // `wants_mouse_lock` is true on the frame a left-click occurred while
    // the mouse is NOT in relative mode. Upper layers use it to re-lock
    // the mouse (analogous to clicking the MC window to re-enter game).
    bool wants_mouse_lock = false;
};

}  // namespace snt::input

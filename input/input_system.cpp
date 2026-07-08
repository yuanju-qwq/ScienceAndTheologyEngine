// InputSystem implementation.

#include "input/input_system.h"

// SDL.h pulls in SDL_Event + all sub-types. The header only forward-
// declares SDL_Event to keep SDL out of the public API; the .cpp needs
// the full definition to read event fields.
#include <SDL3/SDL.h>

#include <cstring>

namespace snt::input {

void InputSystem::new_frame() {
    // Clear per-frame edge state. Held state is refreshed in end_frame().
    std::memset(state_.key_pressed, 0, sizeof(state_.key_pressed));
    std::memset(state_.mouse_pressed, 0, sizeof(state_.mouse_pressed));
    state_.mouse_dx = 0.0f;
    state_.mouse_dy = 0.0f;
    state_.esc_pressed = false;
    state_.wants_mouse_lock = false;
}

void InputSystem::process_event(const void* sdl_event) {
    // Cast back to SDL_Event*; SDL headers are included in this TU.
    const SDL_Event& event = *static_cast<const SDL_Event*>(sdl_event);

    switch (event.type) {
        case SDL_EVENT_QUIT:
            state_.quit_requested = true;
            break;

        case SDL_EVENT_KEY_DOWN: {
            // SDL3 event.key.key is a SDL_Keycode; scancode is event.key.scancode.
            // Use scancode for array indexing (stable across layouts).
            int scancode = static_cast<int>(event.key.scancode);
            if (scancode >= 0 && scancode < static_cast<int>(kKeyCount)) {
                // Only mark pressed on the initial transition (not repeats).
                // SDL3 sets event.key.repeat to non-zero for auto-repeat.
                if (event.key.repeat == 0) {
                    state_.key_pressed[scancode] = true;
                }
                state_.key_held[scancode] = true;
            }
            // P2.A2: ESC sets the edge flag for upper layers to consume.
            // We do NOT close the window here (Window no longer does either).
            if (event.key.key == SDLK_ESCAPE && event.key.repeat == 0) {
                state_.esc_pressed = true;
            }
            break;
        }

        case SDL_EVENT_KEY_UP: {
            int scancode = static_cast<int>(event.key.scancode);
            if (scancode >= 0 && scancode < static_cast<int>(kKeyCount)) {
                state_.key_held[scancode] = false;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            // SDL3: event.button.button is 1=Left, 2=Middle, 3=Right.
            // Map to 0/1/2 for our array.
            uint32_t idx = 0;
            if (event.button.button == SDL_BUTTON_LEFT)        idx = 0;
            else if (event.button.button == SDL_BUTTON_MIDDLE) idx = 1;
            else if (event.button.button == SDL_BUTTON_RIGHT)  idx = 2;
            else break;  // other buttons (X1/X2) ignored for now
            state_.mouse_pressed[idx] = true;
            state_.mouse_held[idx] = true;
            // P2.A2: a left click signals "wants_mouse_lock" so the Engine
            // can re-enter relative mouse mode (MC-style: click to play).
            // The Engine only honors this when not already locked.
            if (idx == 0) {
                state_.wants_mouse_lock = true;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            uint32_t idx = 0;
            if (event.button.button == SDL_BUTTON_LEFT)        idx = 0;
            else if (event.button.button == SDL_BUTTON_MIDDLE) idx = 1;
            else if (event.button.button == SDL_BUTTON_RIGHT)  idx = 2;
            else break;
            state_.mouse_held[idx] = false;
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            // Accumulate relative motion; absolute position also updated.
            state_.mouse_dx += static_cast<float>(event.motion.xrel);
            state_.mouse_dy += static_cast<float>(event.motion.yrel);
            state_.mouse_x = event.motion.x;
            state_.mouse_y = event.motion.y;
            break;
        }

        default:
            break;
    }
}

void InputSystem::end_frame() {
    // Refresh held-key state from SDL's snapshot. This catches keys that
    // were pressed/released between events (rare, but possible under
    // high load) and provides authoritative held state.
    const bool* keys = SDL_GetKeyboardState(nullptr);
    static_assert(sizeof(bool) == sizeof(state_.key_held[0]),
                  "SDL bool size mismatch — expected 1 byte");
    std::memcpy(state_.key_held, keys, sizeof(state_.key_held));

    // Also refresh mouse held state via SDL_GetMouseState, to catch
    // button state that bypassed our event handler (e.g. if events were
    // dropped).
    Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
    state_.mouse_held[0] = (mouse & SDL_BUTTON_LMASK) != 0;
    state_.mouse_held[1] = (mouse & SDL_BUTTON_MMASK) != 0;
    state_.mouse_held[2] = (mouse & SDL_BUTTON_RMASK) != 0;
}

}  // namespace snt::input

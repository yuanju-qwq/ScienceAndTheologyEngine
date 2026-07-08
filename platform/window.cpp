// Platform window implementation using SDL3.
// P1.1: minimal stub — SDL init + window create + poll events.
// P1.2 will add full input handling.

#define SNT_LOG_CHANNEL "platform"
#include "core/log.h"

#include "window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>  // SDL_Vulkan_* functions

// VK_NO_PROTOTYPES (set via CMake) makes vulkan.h expose types only,
// no function prototypes. VK_NULL_HANDLE + VkSurfaceKHR become available.
#include <vulkan/vulkan.h>

#include <format>

namespace snt::platform {

Window::Window() = default;
Window::~Window() { destroy(); }

Window::Window(Window&& other) noexcept
    : _window(other._window), _should_close(other._should_close) {
    other._window = nullptr;
    other._should_close = false;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        _window = other._window;
        _should_close = other._should_close;
        other._window = nullptr;
        other._should_close = false;
    }
    return *this;
}

snt::core::Expected<void> Window::create(const WindowDesc& desc) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return snt::core::Error{snt::core::ErrorCode::kPlatformInitFailed,
                                std::format("SDL_Init failed: {}", SDL_GetError())};
    }

    SDL_WindowFlags flags = 0;
    if (desc.vulkan_enabled) flags |= SDL_WINDOW_VULKAN;
    if (desc.resizable)      flags |= SDL_WINDOW_RESIZABLE;
    if (desc.fullscreen)     flags |= SDL_WINDOW_FULLSCREEN;

    _window = SDL_CreateWindow(
        std::string(desc.title).c_str(),
        desc.width,
        desc.height,
        flags
    );
    if (!_window) {
        return snt::core::Error{snt::core::ErrorCode::kWindowCreateFailed,
                                std::format("SDL_CreateWindow failed: {}", SDL_GetError())};
    }

    _should_close = false;
    return {};
}

void Window::destroy() {
    if (_window) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(_window));
        _window = nullptr;
    }
    // SDL_Quit deferred until process exit; multiple windows may share SDL.
}

bool Window::poll_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Forward every event to the registered callback first, so the
        // input layer sees the full event stream including quit/ESC.
        if (event_callback_) {
            event_callback_(static_cast<const void*>(&event));
        }

        switch (event.type) {
            case SDL_EVENT_QUIT:
                _should_close = true;
                return false;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(static_cast<SDL_Window*>(_window))) {
                    _should_close = true;
                    return false;
                }
                break;
            // ESC no longer closes the window. Upper layers (Engine) read
            // esc_pressed from InputState + decide whether to unlock the
            // mouse or quit. This matches MC-style behavior where ESC
            // opens a menu / releases pointer lock rather than exiting.
            default:
                break;
        }
    }
    return !_should_close;
}

void* Window::native_handle() const {
    return _window;
}

WindowSize Window::size() const {
    int w = 0, h = 0;
    if (_window) {
        SDL_GetWindowSize(static_cast<SDL_Window*>(_window), &w, &h);
    }
    return WindowSize{w, h};
}

// ---------------------------------------------------------------------------
// Vulkan surface creation (P1.3).
// SDL3 provides SDL_Vulkan_CreateSurface() which handles the platform-
// specific surface creation (Win32/X11/Wayland) behind one call.
// ---------------------------------------------------------------------------

snt::core::Expected<void> Window::create_vulkan_surface(void* vk_instance, uint64_t* out_surface) {
    if (!_window || !vk_instance || !out_surface) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "create_vulkan_surface: invalid args"};
    }

    // SDL3's SDL_VulkanSurface is an opaque handle; cast to VkSurfaceKHR.
    // SDL3 uses `VkSurfaceKHR` (a uint64_t typedef on most platforms).
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(_window),
                                  static_cast<VkInstance>(vk_instance),
                                  nullptr,  // allocation callbacks (use default)
                                  &surface)) {
        return snt::core::Error{snt::core::ErrorCode::kSurfaceCreateFailed,
                                std::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError())};
    }

    *out_surface = reinterpret_cast<uint64_t>(surface);
    return {};
}

const char* const* Window::sdl_vulkan_instance_extensions(uint32_t* count) {
    // SDL3 returns a const char* const* array of extension names that must
    // be enabled when creating the VkInstance for surface support.
    return SDL_Vulkan_GetInstanceExtensions(count);
}

// ---------------------------------------------------------------------------
// Relative mouse mode (pointer lock) — P2.A2 MC-style free-look.
// SDL3's SDL_SetWindowRelativeMouseMode confines the mouse to the window
// + hides the cursor + reports relative motion via event.motion.xrel/yrel.
// ---------------------------------------------------------------------------

snt::core::Expected<void> Window::set_relative_mouse_mode(bool enabled) {
    if (!_window) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "set_relative_mouse_mode: window not created"};
    }
    if (!SDL_SetWindowRelativeMouseMode(static_cast<SDL_Window*>(_window), enabled)) {
        return snt::core::Error{snt::core::ErrorCode::kPlatformInitFailed,
                                std::format("SDL_SetWindowRelativeMouseMode failed: {}", SDL_GetError())};
    }
    return {};
}

bool Window::relative_mouse_mode() const {
    if (!_window) return false;
    return SDL_GetWindowRelativeMouseMode(static_cast<SDL_Window*>(_window));
}

} // namespace snt::platform

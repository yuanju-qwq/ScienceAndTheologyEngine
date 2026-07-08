#pragma once

// Platform window abstraction built on top of SDL3.
// P1.3 adds Vulkan surface creation (platform layer owns the surface
// because it's tightly coupled to the native window handle).
// P2.A1 adds an event callback so the input layer can ingest SDL events
// without the platform layer depending on the input module.
//
// Vulkan handles (VkInstance/VkSurfaceKHR) are passed as void* to avoid
// pulling <vulkan/vulkan.h> into the platform layer. Callers in
// render_backend cast between void* and the real Vulkan types.

#include <cstdint>
#include <functional>
#include <string_view>

#include "core/expected.h"  // Expected<void> for create / create_vulkan_surface / set_relative_mouse_mode

namespace snt::platform {

struct WindowSize {
    int width;
    int height;
};

// Window creation descriptor.
struct WindowDesc {
    std::string_view title;
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool resizable = true;
    // Vulkan surface will be created by render_backend in P1.3.
    bool vulkan_enabled = true;
};

// RAII window. Destructor releases SDL resources.
class Window {
public:
    // Callback type for SDL event forwarding. The Window calls this for
    // every polled SDL_Event so upper layers (InputSystem) can react
    // without depending on SDL directly. The event pointer is valid only
    // for the duration of the call.
    using EventCallback = std::function<void(const void* sdl_event)>;

    Window();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    // Create the window. Returns an Error on failure (SDL error logged).
    snt::core::Expected<void> create(const WindowDesc& desc);
    void destroy();

    // Register a callback to receive each polled SDL_Event. Pass nullptr
    // to unregister. The callback is invoked during poll_events().
    void set_event_callback(EventCallback cb) { event_callback_ = std::move(cb); }

    // Poll window events. Returns false if window requested close.
    // For each polled event, the registered event callback (if any) is
    // invoked BEFORE window-level handling (quit/ESC) — this lets input
    // see every event including the quit one.
    bool poll_events();

    // Swap chain presentation is handled in render_backend; this only
    // exposes the native window handle for surface creation.
    void* native_handle() const;

    WindowSize size() const;
    bool should_close() const { return _should_close; }

    // Create a Vulkan surface backed by this window.
    // `vk_instance` is a VkInstance cast to void*; `out_surface` receives
    // the resulting VkSurfaceKHR cast to uint64_t.
    // The instance must be created with the extensions returned by
    // sdl_vulkan_instance_extensions().
    // Returns an Error on failure.
    snt::core::Expected<void> create_vulkan_surface(void* vk_instance, uint64_t* out_surface);

    // SDL-required Vulkan instance extensions for window surface creation.
    // Call before vkCreateInstance and add these to VkInstanceCreateInfo.
    // Returns a pointer to a static array of extension name strings
    // (valid until SDL_Quit). `count` receives the array length.
    const char* const* sdl_vulkan_instance_extensions(uint32_t* count);

    // Toggle SDL's relative mouse mode (pointer lock). When enabled, the
    // mouse is confined to the window + hidden; SDL reports relative
    // motion via event.motion.xrel/yrel. Used for MC-style free-look.
    // Returns an Error on failure.
    snt::core::Expected<void> set_relative_mouse_mode(bool enabled);

    // Query whether relative mouse mode is currently active.
    bool relative_mouse_mode() const;

private:
    void* _window = nullptr;        // SDL_Window*
    bool _should_close = false;
    EventCallback event_callback_;  // optional; called per SDL event
};

} // namespace snt::platform

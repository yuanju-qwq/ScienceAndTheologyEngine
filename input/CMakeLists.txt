# Input: decoupled input abstraction layer.
# P2.A1: InputSystem consumes SDL events and exposes an InputState snapshot
# that ECS systems (CameraSystem etc.) read each frame.
#
# Layering: input sits beside platform — it consumes SDL events but does
# not own the window. platform/window emits raw SDL events via callback;
# input translates them into a queryable InputState.

add_library(snt_input STATIC
    input_system.cpp
)
target_include_directories(snt_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_input PUBLIC snt_engine_settings)
# SDL3 for SDL_Event / SDL_GetKeyboardState types used in the public API.
target_link_libraries(snt_input PRIVATE snt_third_party)

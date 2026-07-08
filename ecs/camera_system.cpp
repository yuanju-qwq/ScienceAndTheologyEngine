// Camera System implementation — MC-style first-person controls.
//
// P2.A2: rewritten for Minecraft creative-mode feel.
//   - W/A/S/D: horizontal movement (forward/strafe on XZ plane)
//   - Space / LShift: ascend / descend
//   - Double-tap W (within 400ms): sprint (2x speed while W held)
//   - Mouse free-look: only active when mouse_locked_ (Engine sets this
//     based on SDL's relative mouse mode)
//
// Sprint detection uses key_pressed (edge event) rather than key_held so
// each tap is counted exactly once. The accumulator advances by dt so
// timestamps are monotonically increasing (no chrono dependency).

#include "ecs/camera_system.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "input/input_system.h"

#include <SDL3/SDL_scancode.h>

#include <cmath>

namespace snt::ecs {

void CameraSystem::update(World& world, float dt) {
    if (!input_ || active_camera_ == entt::null) return;

    auto& registry = world.registry();
    if (!registry.all_of<Transform, Camera>(active_camera_)) return;

    auto& transform = registry.get<Transform>(active_camera_);
    const auto& state = input_->state();

    time_accumulator_ += dt;

    // --- Sprint state machine (double-tap W within 400ms) ---
    // Detect edge press of W. If the previous W press was < 400ms ago,
    // enter sprint. Releasing W exits sprint (must double-tap again).
    static constexpr float kSprintWindow = 0.4f;  // seconds
    if (state.key_pressed[SDL_SCANCODE_W]) {
        if (last_w_press_time_ >= 0.0f &&
            (time_accumulator_ - last_w_press_time_) < kSprintWindow) {
            sprint_active_ = true;
        }
        last_w_press_time_ = time_accumulator_;
    }
    // Exit sprint when W is released.
    if (!state.key_held[SDL_SCANCODE_W]) {
        sprint_active_ = false;
    }

    // --- Movement speed ---
    float speed = move_speed_ * dt;
    if (sprint_active_) {
        speed *= 2.0f;
    }

    // --- Forward + right vectors (horizontal plane only for MC feel) ---
    // Yaw rotates the forward vector around Y. Pitch only affects look,
    // not movement direction — MC creative flight moves on XZ regardless
    // of where the camera points vertically.
    // Convention MUST match RenderSystem::build_view_matrix so the camera
    // moves toward where it looks:
    //   forward = (cos(yaw), 0, sin(yaw))
    //   right   = up × forward = (-sin(yaw), 0, cos(yaw))
    // (Cross product order matters: up × forward gives the player's right
    // hand side in a right-handed Y-up coordinate system.)
    // At yaw=-90°: forward=(0,0,-1) looking -Z, right=(1,0,0) pointing +X.
    float yaw_rad = yaw_ * 3.14159265f / 180.0f;
    float forward[3] = {
        std::cos(yaw_rad),
        0.0f,
        std::sin(yaw_rad),
    };
    float right[3] = {
        -std::sin(yaw_rad),
        0.0f,
        std::cos(yaw_rad),
    };

    // --- WASD movement (held state) ---
    if (state.key_held[SDL_SCANCODE_W]) {
        transform.position[0] += forward[0] * speed;
        transform.position[2] += forward[2] * speed;
    }
    if (state.key_held[SDL_SCANCODE_S]) {
        transform.position[0] -= forward[0] * speed;
        transform.position[2] -= forward[2] * speed;
    }
    if (state.key_held[SDL_SCANCODE_A]) {
        transform.position[0] -= right[0] * speed;
        transform.position[2] -= right[2] * speed;
    }
    if (state.key_held[SDL_SCANCODE_D]) {
        transform.position[0] += right[0] * speed;
        transform.position[2] += right[2] * speed;
    }

    // --- Vertical movement (Space=up, LShift=down) ---
    if (state.key_held[SDL_SCANCODE_SPACE]) {
        transform.position[1] += speed;
    }
    if (state.key_held[SDL_SCANCODE_LSHIFT]) {
        transform.position[1] -= speed;
    }

    // --- Mouse look (only when pointer is locked by Engine) ---
    if (mouse_locked_) {
        yaw_ += state.mouse_dx * look_speed_;
        pitch_ -= state.mouse_dy * look_speed_;  // SDL y down → invert for pitch up

        // Clamp pitch to avoid gimbal flip.
        if (pitch_ > 89.0f) pitch_ = 89.0f;
        if (pitch_ < -89.0f) pitch_ = -89.0f;
    }

    // Write rotation into transform for the render system to use.
    // RenderSystem's build_view_matrix reads rotation[0]=pitch, [1]=yaw.
    transform.rotation[0] = pitch_;
    transform.rotation[1] = yaw_;
    transform.rotation[2] = 0.0f;
}

}  // namespace snt::ecs

#pragma once

#include "core/events.h"
#include "ecs/entt_config.h"
#include "ecs/system.h"
#include "player/voxel_collision.h"

#include <string>
#include <utility>

namespace snt::data { class ChunkRegistry; }
namespace snt::input { class InputSystem; }
namespace snt::voxel { class ChunkRenderSystem; }

namespace snt::player {

struct PlayerControllerTuning {
    float move_speed = 4.3f;
    float sprint_multiplier = 1.45f;
    float jump_speed = 6.2f;
    float gravity = 20.0f;
    float terminal_velocity = 48.0f;
    float look_speed = 0.1f;
    float reach_distance = 6.0f;
    float width = 0.6f;
    float height = 1.8f;
    float eye_height = 1.62f;
};

struct PlayerControllerState {
    Vec3 feet_position{4.0f, 8.0f, 8.0f};
    Vec3 velocity{};
    float yaw = -90.0f;
    float pitch = -25.0f;
    bool grounded = false;
};

class PlayerControllerSystem : public snt::ecs::System {
public:
    PlayerControllerSystem() = default;
    ~PlayerControllerSystem() override = default;

    void set_input(snt::input::InputSystem* input) { input_ = input; }
    void set_chunk_registry(snt::data::ChunkRegistry* registry) { chunk_registry_ = registry; }
    void set_chunk_render_system(snt::voxel::ChunkRenderSystem* system) {
        chunk_render_system_ = system;
    }
    void set_camera_entity(entt::entity camera) { camera_entity_ = camera; }
    void set_dimension_id(std::string dimension_id) { dimension_id_ = std::move(dimension_id); }
    void set_tuning(const PlayerControllerTuning& tuning) { tuning_ = tuning; }
    void set_spawn_feet_position(Vec3 p) { state_.feet_position = p; }
    void set_initial_look(float yaw, float pitch);

    const PlayerControllerState& state() const { return state_; }

    void set_mouse_locked(bool locked) { mouse_locked_ = locked; }
    void on_mouse_lock_changed(const snt::core::MouseLockChanged& evt) {
        set_mouse_locked(evt.locked);
    }

    void update(snt::ecs::World& world, float dt) override;

private:
    Aabb current_body_aabb() const;
    Vec3 eye_position() const;
    Vec3 look_direction() const;
    void sync_camera_transform(snt::ecs::World& world);
    void try_break_target_block();

    snt::input::InputSystem* input_ = nullptr;
    snt::data::ChunkRegistry* chunk_registry_ = nullptr;
    snt::voxel::ChunkRenderSystem* chunk_render_system_ = nullptr;
    entt::entity camera_entity_ = entt::null;

    std::string dimension_id_ = "overworld";
    PlayerControllerTuning tuning_{};
    PlayerControllerState state_{};
    bool mouse_locked_ = false;
};

}  // namespace snt::player

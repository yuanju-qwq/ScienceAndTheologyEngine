// PlayerPhysicsSystem implementation.

#include "player/player_physics_system.h"

#include "voxel/data/chunk_registry.h"
#include "render/render_components.h"
#include "ecs/world.h"
#include "ecs/world_command_queue.h"
#include "player/collision_snapshot.h"

#include <memory>
#include <utility>

namespace snt::player {
namespace {

Aabb player_body_aabb(const PlayerControllerState& state,
                      const PlayerControllerTuning& tuning) {
    const float half_width = tuning.width * 0.5f;
    return {
        .min = {
            state.feet_position.x - half_width,
            state.feet_position.y,
            state.feet_position.z - half_width,
        },
        .max = {
            state.feet_position.x + half_width,
            state.feet_position.y + tuning.height,
            state.feet_position.z + half_width,
        },
    };
}

void sync_camera_transform(snt::ecs::World& world,
                           entt::entity player_entity,
                           const PlayerControllerState& state,
                           const PlayerControllerTuning& tuning) {
    auto& registry = world.registry();
    if (!registry.valid(player_entity) ||
        !registry.all_of<snt::render::Transform>(player_entity)) {
        return;
    }

    auto& transform = registry.get<snt::render::Transform>(player_entity);
    transform.position[0] = state.feet_position.x;
    transform.position[1] = state.feet_position.y + tuning.eye_height;
    transform.position[2] = state.feet_position.z;
    transform.rotation[0] = state.pitch;
    transform.rotation[1] = state.yaw;
    transform.rotation[2] = 0.0f;
}

class PlayerPhysicsTask final : public snt::ecs::IWorkerTask {
public:
    PlayerPhysicsTask(entt::entity player_entity,
                      PlayerControllerState state,
                      PlayerControllerTuning tuning,
                      VoxelCollisionSnapshot collision_snapshot,
                      Vec3 desired_delta)
        : player_entity_(player_entity),
          state_(std::move(state)),
          tuning_(std::move(tuning)),
          collision_snapshot_(std::move(collision_snapshot)),
          desired_delta_(desired_delta) {}

    void execute(snt::ecs::WorkerCommandContext& commands) override {
        PlayerControllerState updated_state = state_;
        const CollisionMoveResult move = move_aabb_collide_voxels(
            collision_snapshot_, player_body_aabb(updated_state, tuning_), desired_delta_);

        updated_state.feet_position.x += move.delta.x;
        updated_state.feet_position.y += move.delta.y;
        updated_state.feet_position.z += move.delta.z;

        if (move.hit_x) {
            updated_state.velocity.x = 0.0f;
        }
        if (move.hit_z) {
            updated_state.velocity.z = 0.0f;
        }
        if (move.hit_y) {
            if (updated_state.velocity.y < 0.0f) {
                updated_state.grounded = true;
            }
            updated_state.velocity.y = 0.0f;
        } else {
            updated_state.grounded = false;
        }

        commands.enqueue([player_entity = player_entity_,
                          state = std::move(updated_state),
                          tuning = tuning_](snt::ecs::World& world) {
            auto& registry = world.registry();
            if (!registry.valid(player_entity) ||
                !registry.all_of<PlayerControllerState>(player_entity)) {
                return;
            }

            registry.get<PlayerControllerState>(player_entity) = state;
            sync_camera_transform(world, player_entity, state, tuning);
        });
    }

private:
    entt::entity player_entity_ = entt::null;
    PlayerControllerState state_{};
    PlayerControllerTuning tuning_{};
    VoxelCollisionSnapshot collision_snapshot_{};
    Vec3 desired_delta_{};
};

}  // namespace

PlayerPhysicsSystem::PlayerPhysicsSystem(
    const snt::voxel::ChunkRegistry* chunk_registry,
    entt::entity player_entity,
    std::string dimension_id,
    PlayerControllerTuning tuning)
    : chunk_registry_(chunk_registry),
      player_entity_(player_entity),
      dimension_id_(std::move(dimension_id)),
      tuning_(std::move(tuning)) {}

std::unique_ptr<snt::ecs::IWorkerTask> PlayerPhysicsSystem::capture(
    const snt::ecs::World& world, float dt) {
    if (!chunk_registry_ || player_entity_ == entt::null) {
        return nullptr;
    }

    const auto& registry = world.registry();
    if (!registry.valid(player_entity_) ||
        !registry.all_of<PlayerControllerState>(player_entity_)) {
        return nullptr;
    }

    const PlayerControllerState state =
        registry.get<PlayerControllerState>(player_entity_);
    const Vec3 desired_delta{
        state.velocity.x * dt,
        state.velocity.y * dt,
        state.velocity.z * dt,
    };
    const CollisionWorldView world_view(chunk_registry_, dimension_id_, false);
    return std::make_unique<PlayerPhysicsTask>(
        player_entity_, state, tuning_,
        VoxelCollisionSnapshot::capture(world_view, player_body_aabb(state, tuning_), desired_delta),
        desired_delta);
}

}  // namespace snt::player

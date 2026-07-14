// PlayerPhysicsSystem -- value-snapshot collision worker.
//
// The system captures PlayerControllerState and VoxelCollisionSnapshot on
// the main thread, performs only pure movement integration on a worker, then
// writes the state and camera transform through the scheduler barrier.

#pragma once

#include "ecs/system.h"
#include "player/player_controller.h"

#include <memory>
#include <string>

namespace snt::voxel { class ChunkRegistry; }

namespace snt::player {

class PlayerPhysicsSystem final : public snt::ecs::IWorkerSystem {
public:
    PlayerPhysicsSystem(const snt::voxel::ChunkRegistry* chunk_registry,
                        entt::entity player_entity,
                        std::string dimension_id,
                        PlayerControllerTuning tuning);

    snt::ecs::SystemMetadata metadata() const override {
        return {
            "player.physics",
            snt::ecs::SystemThreadAffinity::Worker,
            {
                {"player.controller_state", snt::ecs::SystemResourceAccessMode::Read},
                {"player.controller_state", snt::ecs::SystemResourceAccessMode::Write},
                {"world.chunks", snt::ecs::SystemResourceAccessMode::Read},
                {"ecs.camera_transform", snt::ecs::SystemResourceAccessMode::Write},
            },
        };
    }

    std::unique_ptr<snt::ecs::IWorkerTask> capture(
        const snt::ecs::World& world, float dt) override;

private:
    const snt::voxel::ChunkRegistry* chunk_registry_ = nullptr;
    entt::entity player_entity_ = entt::null;
    std::string dimension_id_;
    PlayerControllerTuning tuning_{};
};

}  // namespace snt::player

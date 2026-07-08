// System — base class for ECS systems.
//
// Systems contain logic that operates on entities with specific components.
// Each system has an update(dt) method called once per frame by the World.
//
// P1.5: RenderSystem (draws meshes), CameraSystem (handles WASD input).
// P2+: PhysicsSystem, AISystem, etc.

#pragma once

#include <cstdint>

namespace snt::ecs {

class World;

// Base class for all ECS systems.
class System {
public:
    virtual ~System() = default;

    // Called once per frame. `dt` is delta time in seconds.
    virtual void update(World& world, float dt) = 0;
};

}  // namespace snt::ecs

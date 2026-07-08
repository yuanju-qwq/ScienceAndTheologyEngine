// World — owns the ECS registry and manages systems.
//
// Wraps entt::registry, providing:
//   - entity creation + component attachment
//   - system registration + per-frame update
//   - view/group access for systems to query entities
//   - stable EntityGuid <-> entt::entity mapping for serialization
//
// EntityGuid mapping (Phase 1):
//   - create_entity() issues a fresh Guid via the generator and attaches
//     it as an EntityGuid component. Callers that need a specific Guid
//     (e.g. scene loader) use create_entity_with_guid(guid).
//   - The reverse map (Guid -> entt::entity) is maintained AUTOMATICALLY
//     by entt's on_construct<EntityGuid> / on_destroy<EntityGuid> sinks,
//     so add_component(EntityGuid) and registry.destroy() stay in sync
//     without any caller-side bookkeeping.
//   - find_entity_by_guid(guid) returns entt::null if not found.
//
// P1.5: basic entity + system management.
// P2+: system scheduling (dependencies, parallel update via JobSystem).

#pragma once

// EnTT assertions must be routed through our SNT_LOG_FATAL + SNT_DEBUGBREAK
// path (see ecs/entt_config.h for the full rationale).
#include "ecs/entt_config.h"

#include "ecs/entity_guid.h"
#include "ecs/system.h"  // System::update() called inline in World::update()

#include <memory>
#include <unordered_map>
#include <vector>

namespace snt::ecs {

class World {
public:
    World();
    ~World() = default;

    // Non-copyable; the registry owns entity data.
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // --- Entity management ---

    // Create a new entity with a freshly generated EntityGuid.
    // The Guid is attached as an EntityGuid component so it serializes
    // with the entity and can be queried via view<EntityGuid>.
    entt::entity create_entity();

    // Create an entity with a specific EntityGuid. Used by scene loaders
    // that must preserve Guids across save/load cycles. If `guid` is
    // already in use, returns entt::null and logs an error.
    entt::entity create_entity_with_guid(EntityGuid guid);

    // Destroy an entity and all its components. The EntityGuid reverse
    // map is updated automatically via the on_destroy sink.
    void destroy_entity(entt::entity e);

    // Look up an entity by its stable Guid. Returns entt::null if no
    // live entity has this Guid (e.g. it was destroyed, or the Guid was
    // never issued).
    entt::entity find_entity_by_guid(EntityGuid guid) const;

    // Read the Guid attached to an entity. Returns kInvalidEntityGuid if
    // the entity has no EntityGuid component (should not happen for
    // entities created via World::create_entity*, but is possible for
    // raw registry.create() callers).
    EntityGuid guid_of(entt::entity e) const;

    // Attach a component to an entity. Returns a reference to the component.
    template<typename Component, typename... Args>
    Component& add_component(entt::entity e, Args&&... args) {
        return registry_.emplace<Component>(e, std::forward<Args>(args)...);
    }

    // Get a component from an entity.
    template<typename Component>
    Component& get_component(entt::entity e) {
        return registry_.get<Component>(e);
    }

    // Get a component from an entity (const overload).
    template<typename Component>
    const Component& get_component(entt::entity e) const {
        return registry_.get<Component>(e);
    }

    // --- System management ---

    // Register a system. World takes ownership.
    template<typename SystemType, typename... Args>
    SystemType& add_system(Args&&... args) {
        auto sys = std::make_unique<SystemType>(std::forward<Args>(args)...);
        auto& ref = *sys;
        systems_.push_back(std::move(sys));
        return ref;
    }

    // --- Per-frame update ---
    // Calls update(dt) on all registered systems in registration order.
    void update(float dt) {
        for (auto& sys : systems_) {
            sys->update(*this, dt);
        }
    }

    // --- Registry access (for systems to query entities) ---
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

    // --- Guid generator access ---
    // Exposed for tests and for save managers that need to persist the
    // next-issuable Guid. Normal callers should use create_entity().
    EntityGuidGenerator& guid_generator() { return guid_generator_; }
    const EntityGuidGenerator& guid_generator() const { return guid_generator_; }

private:
    entt::registry registry_;
    std::vector<std::unique_ptr<System>> systems_;

    // Monotonic Guid issuer. Lives in World so all entities created
    // through this World share a single counter space.
    EntityGuidGenerator guid_generator_;

    // Reverse map: EntityGuid -> live entt::entity. Maintained
    // automatically by entt sinks registered in the World constructor.
    std::unordered_map<EntityGuid, entt::entity> guid_to_entity_;

    // Sink connections are stored to keep them alive for the World's
    // lifetime. entt::connection is lightweight (2 pointers).
    std::vector<entt::connection> sink_connections_;

    // --- entt sink callbacks for EntityGuid <-> entity sync ---
    // on_construct<EntityGuid>: insert (guid -> entity) into the reverse
    //   map. Fires during emplace<EntityGuid> in create_entity*.
    // on_destroy<EntityGuid>: evict the guid from the reverse map. Fires
    //   when the component is removed or when the entity is destroyed
    //   (entt destroys all components first).
    // Defined inline here so the sink template instantiation in world.cpp
    // can see them; bodies live in world.cpp.
    void on_guid_constructed(entt::registry& reg, entt::entity e);
    void on_guid_destroyed(entt::registry& reg, entt::entity e);
};

}  // namespace snt::ecs

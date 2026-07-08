// World implementation.
//
// Phase 1: EntityGuid <-> entt::entity mapping is maintained by entt sinks
// registered in the World constructor. on_construct<EntityGuid> adds to
// the reverse map; on_destroy<EntityGuid> removes from it. This keeps
// the map in sync with the registry without any caller-side bookkeeping.

#define SNT_LOG_CHANNEL "ecs"
#include "core/log.h"

#include "ecs/world.h"

namespace snt::ecs {

World::World() {
    // --- on_construct<EntityGuid>: insert into reverse map ---
    // Fires whenever an EntityGuid component is emplaced onto an entity
    // (via create_entity, create_entity_with_guid, or a raw
    // add_component<EntityGuid>). We record guid -> entity so
    // find_entity_by_guid can do O(1) lookups.
    registry_.on_construct<EntityGuid>()
        .connect<&World::on_guid_constructed>(this);

    // --- on_destroy<EntityGuid>: remove from reverse map ---
    // Fires when the EntityGuid component is removed OR when the entity
    // itself is destroyed (entt destroys all components first). Either way
    // we evict the guid from the reverse map.
    registry_.on_destroy<EntityGuid>()
        .connect<&World::on_guid_destroyed>(this);
}

entt::entity World::create_entity() {
    return create_entity_with_guid(guid_generator_.next());
}

entt::entity World::create_entity_with_guid(EntityGuid guid) {
    if (!guid.valid()) {
        SNT_LOG_ERROR("create_entity_with_guid: invalid guid (0)");
        return entt::null;
    }
    if (guid_to_entity_.find(guid) != guid_to_entity_.end()) {
        SNT_LOG_ERROR("create_entity_with_guid: guid %llu already in use",
                      static_cast<unsigned long long>(guid.value));
        return entt::null;
    }

    // Create the entity first, then attach the Guid component. The
    // on_construct<EntityGuid> sink will fire during emplace and insert
    // the (guid -> entity) entry into guid_to_entity_.
    entt::entity e = registry_.create();
    registry_.emplace<EntityGuid>(e, guid);
    return e;
}

void World::destroy_entity(entt::entity e) {
    // registry_.destroy triggers on_destroy<EntityGuid> (if present),
    // which removes the guid from guid_to_entity_.
    registry_.destroy(e);
}

entt::entity World::find_entity_by_guid(EntityGuid guid) const {
    auto it = guid_to_entity_.find(guid);
    if (it == guid_to_entity_.end()) {
        return entt::null;
    }
    return it->second;
}

EntityGuid World::guid_of(entt::entity e) const {
    // entt::registry::try_get returns nullptr if the entity has no such
    // component (or the entity is null/tombstone). We fall back to the
    // invalid sentinel in that case.
    const EntityGuid* g = registry_.try_get<EntityGuid>(e);
    return g ? *g : kInvalidEntityGuid;
}

// ---------------------------------------------------------------------------
// entt sink callbacks (private; declared as file-static free functions
// would also work, but keeping them as member functions lets the sinks
// access guid_to_entity_ directly).
// ---------------------------------------------------------------------------
// NOTE: these are intentionally not in the header because they reference
// entt internals that are only available after entt_config.h is included
// (which the header does, but the bodies would also need to be visible
// to the sink template instantiation — keeping them in the .cpp avoids
// pulling entt sink details into every includer of world.h).

void World::on_guid_constructed(entt::registry& /*reg*/, entt::entity e) {
    // Called inside registry_.emplace<EntityGuid>. The component is
    // already constructed when this fires, so we can read it back.
    const EntityGuid& g = registry_.get<EntityGuid>(e);
    guid_to_entity_[g] = e;
}

void World::on_guid_destroyed(entt::registry& /*reg*/, entt::entity e) {
    // Called before the EntityGuid component is destroyed (entt's
    // on_destroy fires while the component is still readable). Read it
    // back so we know which guid to evict from the reverse map.
    const EntityGuid& g = registry_.get<EntityGuid>(e);
    guid_to_entity_.erase(g);
}

}  // namespace snt::ecs

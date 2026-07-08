// Unit tests for EntityGuid + World's Guid<->entity mapping.
//
// Phase 1: validates that EntityGuid is stable, unique, and that the
// World's reverse map (Guid -> entt::entity) is maintained automatically
// by entt sinks on create / destroy / re-create.

#include "ecs/entity_guid.h"
#include "ecs/world.h"
#include "ecs/components.h"

#include <gtest/gtest.h>

using snt::ecs::EntityGuid;
using snt::ecs::EntityGuidGenerator;
using snt::ecs::kInvalidEntityGuid;
using snt::ecs::World;
using snt::ecs::Transform;

// ===========================================================================
// EntityGuid
// ===========================================================================

TEST(EntityGuidTest, DefaultIsInvalid) {
    EntityGuid g;
    EXPECT_FALSE(g.valid());
    EXPECT_EQ(g.value, 0u);
    EXPECT_EQ(g, kInvalidEntityGuid);
}

TEST(EntityGuidTest, ExplicitValueIsValid) {
    EntityGuid g{42};
    EXPECT_TRUE(g.valid());
    EXPECT_EQ(g.value, 42u);
}

TEST(EntityGuidTest, EqualityIsByValue) {
    EntityGuid a{1};
    EntityGuid b{1};
    EntityGuid c{2};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ===========================================================================
// EntityGuidGenerator
// ===========================================================================

TEST(EntityGuidGeneratorTest, NextNeverReturnsInvalid) {
    EntityGuidGenerator gen;
    for (int i = 0; i < 100; ++i) {
        EntityGuid g = gen.next();
        EXPECT_TRUE(g.valid()) << "iteration " << i;
    }
}

TEST(EntityGuidGeneratorTest, NextIsMonotonicAndUnique) {
    EntityGuidGenerator gen;
    std::unordered_set<EntityGuid> seen;
    for (int i = 0; i < 1000; ++i) {
        EntityGuid g = gen.next();
        // Each issued Guid must be unique within this generator.
        auto [it, inserted] = seen.insert(g);
        EXPECT_TRUE(inserted) << "duplicate Guid issued at iteration " << i;
    }
}

TEST(EntityGuidGeneratorTest, PeekNextDoesNotConsume) {
    EntityGuidGenerator gen;
    EntityGuid peeked = gen.peek_next();
    EntityGuid issued = gen.next();
    EXPECT_EQ(peeked, issued);
    // Next call advances the counter beyond peeked.
    EntityGuid next = gen.next();
    EXPECT_NE(peeked, next);
}

TEST(EntityGuidGeneratorTest, TwoGeneratorsProduceDisjointGuids) {
    // Two generators in the same process should get different seeds, so
    // their issued Guids never collide.
    EntityGuidGenerator a;
    EntityGuidGenerator b;
    std::unordered_set<EntityGuid> from_a;
    std::unordered_set<EntityGuid> from_b;
    for (int i = 0; i < 100; ++i) {
        from_a.insert(a.next());
        from_b.insert(b.next());
    }
    // No Guid issued by `a` should appear in `b`'s set.
    for (const EntityGuid& g : from_a) {
        EXPECT_EQ(from_b.count(g), 0u)
            << "Guid " << g.value << " issued by both generators";
    }
}

TEST(EntityGuidGeneratorTest, ResetCounterPreservesSeed) {
    EntityGuidGenerator gen;
    EntityGuid first = gen.next();   // counter=1
    EntityGuid second = gen.next();  // counter=2
    EXPECT_NE(first, second);

    // reset_counter advances the counter forward (NEVER backward —
    // backward would re-issue already-used Guids). Resetting to a value
    // past `second` makes the next issued Guid share the seed but use
    // a fresh counter slot.
    gen.reset_counter(100);
    EntityGuid after_reset = gen.next();  // counter=101
    EXPECT_NE(after_reset, first);
    EXPECT_NE(after_reset, second);
    // Same seed means same high 32 bits.
    EXPECT_EQ((after_reset.value >> 32), (first.value >> 32));
    // Low 32 bits equal the new counter (101).
    EXPECT_EQ(static_cast<uint32_t>(after_reset.value), 101u);
}

// ===========================================================================
// World + EntityGuid integration
// ===========================================================================

TEST(WorldEntityGuidTest, CreateEntityAttachesGuid) {
    World world;
    entt::entity e = world.create_entity();
    EntityGuid g = world.guid_of(e);
    EXPECT_TRUE(g.valid());
    EXPECT_NE(g, kInvalidEntityGuid);
}

TEST(WorldEntityGuidTest, FindEntityByGuidReturnsEntity) {
    World world;
    entt::entity e = world.create_entity();
    EntityGuid g = world.guid_of(e);
    EXPECT_EQ(world.find_entity_by_guid(g), e);
}

TEST(WorldEntityGuidTest, FindByGuidReturnsNullForUnknownGuid) {
    World world;
    EntityGuid unknown{0xDEADBEEFu};  // not issued by this world's generator
    EXPECT_TRUE(world.find_entity_by_guid(unknown) == entt::null);
}

TEST(WorldEntityGuidTest, EachEntityGetsUniqueGuid) {
    World world;
    std::unordered_set<EntityGuid> guids;
    for (int i = 0; i < 100; ++i) {
        entt::entity e = world.create_entity();
        EntityGuid g = world.guid_of(e);
        auto [it, inserted] = guids.insert(g);
        EXPECT_TRUE(inserted) << "duplicate Guid at iteration " << i;
    }
}

TEST(WorldEntityGuidTest, DestroyEntityEvictsGuidFromReverseMap) {
    World world;
    entt::entity e = world.create_entity();
    EntityGuid g = world.guid_of(e);

    EXPECT_EQ(world.find_entity_by_guid(g), e);
    world.destroy_entity(e);
    EXPECT_TRUE(world.find_entity_by_guid(g) == entt::null);
}

TEST(WorldEntityGuidTest, DestroyAndRecreateDoesNotResurrectGuid) {
    // The critical serialization correctness property: after destroying an
    // entity, its Guid must not map to any live entity even if the
    // registry recycles the entt::entity numeric value for a new entity.
    World world;
    entt::entity e1 = world.create_entity();
    EntityGuid g1 = world.guid_of(e1);
    world.destroy_entity(e1);

    entt::entity e2 = world.create_entity();
    EntityGuid g2 = world.guid_of(e2);

    EXPECT_NE(g1, g2) << "recycled entity must not inherit the old Guid";
    EXPECT_TRUE(world.find_entity_by_guid(g1) == entt::null)
        << "destroyed entity's Guid must not resolve to the new entity";
    EXPECT_EQ(world.find_entity_by_guid(g2), e2);
}

TEST(WorldEntityGuidTest, CreateEntityWithSpecificGuid) {
    World world;
    EntityGuid preset{0x123456789ABCDEF0ull};
    entt::entity e = world.create_entity_with_guid(preset);
    EXPECT_TRUE(e != entt::null);
    EXPECT_EQ(world.guid_of(e), preset);
    EXPECT_EQ(world.find_entity_by_guid(preset), e);
}

TEST(WorldEntityGuidTest, CreateEntityWithDuplicateGuidFails) {
    World world;
    EntityGuid preset{0xCAFEBABE12345678ull};
    entt::entity e1 = world.create_entity_with_guid(preset);
    EXPECT_TRUE(e1 != entt::null);

    entt::entity e2 = world.create_entity_with_guid(preset);
    EXPECT_TRUE(e2 == entt::null);
    // Reverse map must still point to the first entity.
    EXPECT_EQ(world.find_entity_by_guid(preset), e1);
}

TEST(WorldEntityGuidTest, CreateEntityWithInvalidGuidFails) {
    World world;
    entt::entity e = world.create_entity_with_guid(kInvalidEntityGuid);
    EXPECT_TRUE(e == entt::null);
}

TEST(WorldEntityGuidTest, GuidSurvivesComponentAddRemove) {
    // Adding/removing other components must not disturb the Guid mapping.
    World world;
    entt::entity e = world.create_entity();
    EntityGuid g = world.guid_of(e);

    world.add_component<Transform>(e);
    auto& t = world.get_component<Transform>(e);
    t.position[0] = 5.0f;

    EXPECT_EQ(world.find_entity_by_guid(g), e);
    EXPECT_EQ(world.guid_of(e), g);

    // Removing Transform must not affect the Guid.
    world.registry().remove<Transform>(e);
    EXPECT_EQ(world.find_entity_by_guid(g), e);
    EXPECT_EQ(world.guid_of(e), g);
}

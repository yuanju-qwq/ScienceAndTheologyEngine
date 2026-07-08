// Tests for the general-purpose Uuid + UuidGenerator.
//
// Covers:
//   - Default-constructed Uuid is invalid
//   - Generator never returns kInvalidUuid
//   - Generator produces unique + monotonic Uuids
//   - Two generators produce disjoint Uuid sets
//   - reset_counter preserves the seed
//   - peek_next does not consume
//   - Serializer round-trip

#include "core/uuid.h"
#include "core/binary_reader.h"
#include "core/binary_writer.h"
#include "core/serializer.h"

#include <gtest/gtest.h>

#include <unordered_set>

using snt::core::Uuid;
using snt::core::UuidGenerator;
using snt::core::kInvalidUuid;
using snt::core::Serializer;

// ===========================================================================
// Uuid value type
// ===========================================================================

TEST(UuidTest, DefaultIsInvalid) {
    Uuid u;
    EXPECT_FALSE(u.valid());
    EXPECT_EQ(u, kInvalidUuid);
    EXPECT_EQ(u.low,  0u);
    EXPECT_EQ(u.high, 0u);
}

TEST(UuidTest, ExplicitValuesAreValid) {
    Uuid u{1, 2};
    EXPECT_TRUE(u.valid());
    EXPECT_NE(u, kInvalidUuid);
}

TEST(UuidTest, EqualityIsByValue) {
    Uuid a{1, 2};
    Uuid b{1, 2};
    Uuid c{2, 1};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(UuidTest, OneNonZeroHalfIsValid) {
    // Validity is "either half nonzero" — both {1,0} and {0,1} are valid.
    // The temporary avoids the brace-init comma being parsed as a macro
    // argument separator by EXPECT_TRUE.
    Uuid a{1, 0};
    Uuid b{0, 1};
    EXPECT_TRUE(a.valid());
    EXPECT_TRUE(b.valid());
}

// ===========================================================================
// UuidGenerator
// ===========================================================================

TEST(UuidGeneratorTest, NextNeverReturnsInvalid) {
    UuidGenerator gen;
    for (int i = 0; i < 100; ++i) {
        Uuid u = gen.next();
        EXPECT_TRUE(u.valid()) << "iteration " << i;
        EXPECT_NE(u, kInvalidUuid) << "iteration " << i;
    }
}

TEST(UuidGeneratorTest, NextIsUnique) {
    UuidGenerator gen;
    std::unordered_set<Uuid> seen;
    for (int i = 0; i < 1000; ++i) {
        Uuid u = gen.next();
        auto [it, inserted] = seen.insert(u);
        EXPECT_TRUE(inserted) << "duplicate Uuid at iteration " << i;
    }
}

TEST(UuidGeneratorTest, PeekNextDoesNotConsume) {
    UuidGenerator gen;
    Uuid peeked = gen.peek_next();
    Uuid issued = gen.next();
    EXPECT_EQ(peeked, issued);
    // Subsequent next() advances past peeked.
    Uuid next = gen.next();
    EXPECT_NE(peeked, next);
}

TEST(UuidGeneratorTest, TwoGeneratorsProduceDisjointUuids) {
    // Two generators in the same process should get different seeds, so
    // their issued Uuids never collide.
    UuidGenerator a;
    UuidGenerator b;
    std::unordered_set<Uuid> from_a;
    std::unordered_set<Uuid> from_b;
    for (int i = 0; i < 100; ++i) {
        from_a.insert(a.next());
        from_b.insert(b.next());
    }
    // No Uuid issued by `a` should appear in `b`'s set.
    for (const Uuid& u : from_a) {
        EXPECT_EQ(from_b.count(u), 0u)
            << "Uuid {" << u.low << "," << u.high
            << "} issued by both generators";
    }
}

TEST(UuidGeneratorTest, HighBitsAreStableAcrossCalls) {
    // The high 64 bits of issued Uuids come from seed_high_, which is
    // constant for a given generator. Verifies the layout documented in
    // uuid.h: high = pure seed_high_, low = seed_low_ ^ counter.
    UuidGenerator gen;
    Uuid first  = gen.next();
    Uuid second = gen.next();
    Uuid third  = gen.next();
    EXPECT_EQ(first.high,  second.high);
    EXPECT_EQ(second.high, third.high);
    // Low bits should differ (different counter values folded in).
    EXPECT_NE(first.low,  second.low);
    EXPECT_NE(second.low, third.low);
}

TEST(UuidGeneratorTest, ResetCounterPreservesSeed) {
    UuidGenerator gen;
    Uuid first  = gen.next();   // counter=1
    Uuid second = gen.next();   // counter=2
    EXPECT_NE(first, second);

    gen.reset_counter(100);
    Uuid after_reset = gen.next();  // counter=101

    EXPECT_NE(after_reset, first);
    EXPECT_NE(after_reset, second);
    // Same seed_high_ means same high half.
    EXPECT_EQ(after_reset.high, first.high);
    // Low half = seed_low_ ^ counter, so it differs from first/second
    // (which used counters 1 and 2, not 101).
    EXPECT_NE(after_reset.low, first.low);
    EXPECT_NE(after_reset.low, second.low);
}

TEST(UuidGeneratorTest, LowBitsReflectCounterXor) {
    // Direct verification of the pack() layout: low = seed_low_ ^ counter.
    // We can't read seed_low_ directly (private), but we can recover it:
    //   low(counter=1) ^ 1 == seed_low_
    //   low(counter=2) ^ 2 == seed_low_  (must agree)
    UuidGenerator gen;
    Uuid u1 = gen.next();  // counter=1
    Uuid u2 = gen.next();  // counter=2
    uint64_t seed_low_via_1 = u1.low ^ 1u;
    uint64_t seed_low_via_2 = u2.low ^ 2u;
    EXPECT_EQ(seed_low_via_1, seed_low_via_2);
}

// ===========================================================================
// Serializer<Uuid> round-trip
// ===========================================================================

TEST(UuidSerializerTest, RoundTripPreservesValue) {
    Uuid original{0x0123456789ABCDEFull, 0xFEDCBA9876543210ull};

    snt::core::BinaryWriter writer;
    Serializer<Uuid>::write(writer, original);

    // BinaryReader borrows the writer's buffer (non-owning).
    snt::core::BinaryReader reader(writer.buffer());
    Uuid restored{};
    ASSERT_TRUE(Serializer<Uuid>::read(reader, restored));
    EXPECT_EQ(restored, original);
    EXPECT_TRUE(reader.eof());
}

TEST(UuidSerializerTest, RoundTripOnGeneratedUuid) {
    UuidGenerator gen;
    Uuid original = gen.next();
    ASSERT_TRUE(original.valid());

    snt::core::BinaryWriter writer;
    Serializer<Uuid>::write(writer, original);

    snt::core::BinaryReader reader(writer.buffer());
    Uuid restored{};
    ASSERT_TRUE(Serializer<Uuid>::read(reader, restored));
    EXPECT_EQ(restored, original);
}

TEST(UuidSerializerTest, ReadFailsOnTruncatedInput) {
    Uuid original{1, 2};
    snt::core::BinaryWriter writer;
    Serializer<Uuid>::write(writer, original);

    // Truncate to 8 bytes (only `low` written; `high` missing).
    snt::core::BinaryReader reader(writer.buffer().data(), 8);
    Uuid restored{};
    EXPECT_FALSE(Serializer<Uuid>::read(reader, restored));
}

// ===========================================================================
// std::hash<Uuid> (used by unordered_set/unordered_map)
// ===========================================================================

TEST(UuidHashTest, UsableInUnorderedSet) {
    // Smoke test: std::hash<Uuid> must be invocable and produce stable
    // values so Uuid can key an unordered_set/map. Already exercised by
    // the `seen` sets above; this test pins the capability explicitly.
    UuidGenerator gen;
    std::unordered_set<Uuid> set;
    for (int i = 0; i < 10; ++i) {
        set.insert(gen.next());
    }
    EXPECT_EQ(set.size(), 10u);

    // Same Uuid must hash to the same bucket (lookup works).
    Uuid u = gen.next();
    set.insert(u);
    EXPECT_NE(set.find(u), set.end());
    EXPECT_EQ(set.find(kInvalidUuid), set.end());
}

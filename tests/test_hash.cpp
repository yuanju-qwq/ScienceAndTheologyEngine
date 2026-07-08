// Tests for the FNV-1a hash utilities.
//
// Covers:
//   - Known FNV-1a vectors (empty string, "foobar", etc.)
//   - Determinism: same input -> same output across calls
//   - Differentiation: small input changes flip many output bits
//   - hash_combine asymmetry and distribution

#include "core/hash.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <unordered_set>

using snt::core::hash_string;
using snt::core::hash_combine;

// ===========================================================================
// Known FNV-1a 64-bit vectors (from the FNV reference distribution).
// ===========================================================================
// These pin the implementation to the spec; any change to the algorithm
// or constants breaks these tests immediately.

TEST(HashStringTest, EmptyStringIsOffsetBasis) {
    // FNV-1a of empty input = the offset basis itself (no bytes mixed).
    EXPECT_EQ(hash_string(""), 0xcbf29ce484222325ull);
}

TEST(HashStringTest, KnownVectorFoobar) {
    // FNV-1a 64-bit of "foobar" = 0x85944171f73967e8 (published vector).
    EXPECT_EQ(hash_string("foobar"), 0x85944171f73967e8ull);
}

TEST(HashStringTest, KnownVectorSingleByte) {
    // FNV-1a 64-bit of "a" (0x61):
    //   h = 0xcbf29ce484222325
    //   h ^= 0x61            -> 0xcbf29ce484222344
    //   h *= 0x100000001b3   -> 0xaf63dc4c8601ec8c
    EXPECT_EQ(hash_string("a"), 0xaf63dc4c8601ec8cull);
}

// ===========================================================================
// Determinism + differentiation.
// ===========================================================================

TEST(HashStringTest, SameInputProducesSameHash) {
    const std::string a = "assets/meshes/cube.obj";
    const std::string b = "assets/meshes/cube.obj";
    EXPECT_EQ(hash_string(a), hash_string(b));
}

TEST(HashStringTest, DifferentInputsProduceDifferentHashes) {
    // Sanity: distinct paths must not collide on a 64-bit hash.
    const std::string paths[] = {
        "assets/meshes/cube.obj",
        "assets/meshes/sphere.obj",
        "assets/meshes/cube2.obj",
        "assets/textures/cube.png",
        "assets/meshes/Cube.obj",  // case-sensitive
        "",
    };
    std::unordered_set<uint64_t> seen;
    for (const auto& p : paths) {
        auto [it, inserted] = seen.insert(hash_string(p));
        EXPECT_TRUE(inserted) << "collision on path: " << p;
    }
}

TEST(HashStringTest, StringViewAndStdStringAgree) {
    std::string s = "hello/world.obj";
    std::string_view sv = s;
    EXPECT_EQ(hash_string(s), hash_string(sv));
    EXPECT_EQ(hash_string(sv), hash_string("hello/world.obj"));
}

TEST(HashStringTest, AvalancheFlipsManyBits) {
    // A single-byte change should flip roughly half the output bits.
    // We don't assert an exact count (that would be flaky), just that
    // the difference is large (>= 16 bits out of 64).
    uint64_t h1 = hash_string("assets/cube.obj");
    uint64_t h2 = hash_string("assets/cube2.obj");
    uint64_t diff = h1 ^ h2;
    int bit_count = 0;
    for (int i = 0; i < 64; ++i) {
        bit_count += (diff >> i) & 1u;
    }
    EXPECT_GE(bit_count, 16) << "avalanche too weak for one-byte change";
}

// ===========================================================================
// hash_combine
// ===========================================================================

TEST(HashCombineTest, CombiningDifferentValuesGivesDifferentResults) {
    uint64_t base = hash_string("path");
    uint64_t a = hash_combine(base, 1);
    uint64_t b = hash_combine(base, 2);
    uint64_t c = hash_combine(base, 3);
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(b, c);
}

TEST(HashCombineTest, OrderMatters) {
    // hash_combine is NOT commutative: combine(seed, a) + b != combine(seed, b) + a.
    // This is by design — multi-field keys must hash fields in a fixed order.
    uint64_t seed = 0x1234567890abcdefull;
    uint64_t a = 0xAABBCCDD;
    uint64_t b = 0x11223344;
    uint64_t ab = hash_combine(hash_combine(seed, a), b);
    uint64_t ba = hash_combine(hash_combine(seed, b), a);
    EXPECT_NE(ab, ba);
}

TEST(HashCombineTest, IsDeterministic) {
    uint64_t seed = 42;
    uint64_t v = 100;
    EXPECT_EQ(hash_combine(seed, v), hash_combine(seed, v));
}

TEST(HashCombineTest, SeedChangesResult) {
    uint64_t v = 7;
    EXPECT_NE(hash_combine(1, v), hash_combine(2, v));
    EXPECT_NE(hash_combine(1, v), hash_combine(1, v + 1));
}

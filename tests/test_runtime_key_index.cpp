// Tests for deterministic, immutable runtime key-index snapshots.

#include <array>
#include <string_view>

#include <gtest/gtest.h>

#include "core/runtime_key_index.h"

namespace {

using snt::core::RuntimeKeyId;
using snt::core::RuntimeKeyIndex;

TEST(RuntimeKeyIndexTest, SortsKeysAndAssignsContiguousIdsIndependentOfInputOrder) {
    RuntimeKeyIndex first;
    const std::array<std::string_view, 4> first_keys{
        "zinc.ingot", "charcoal", "iron.ore", "anvil"};
    ASSERT_TRUE(first.rebuild(first_keys));

    RuntimeKeyIndex second;
    const std::array<std::string_view, 4> second_keys{
        "anvil", "iron.ore", "zinc.ingot", "charcoal"};
    ASSERT_TRUE(second.rebuild(second_keys));

    const auto first_snapshot = first.snapshot();
    const auto second_snapshot = second.snapshot();
    ASSERT_EQ(first_snapshot.size(), 4U);
    ASSERT_EQ(second_snapshot.size(), 4U);
    for (const std::string_view key : {"anvil", "charcoal", "iron.ore", "zinc.ingot"}) {
        ASSERT_TRUE(first_snapshot.find_id(key));
        ASSERT_TRUE(second_snapshot.find_id(key));
        EXPECT_EQ(first_snapshot.find_id(key), second_snapshot.find_id(key));
    }

    for (RuntimeKeyId id = 1; id <= 4; ++id) {
        ASSERT_TRUE(first_snapshot.find_key(id));
        ASSERT_TRUE(second_snapshot.find_key(id));
        EXPECT_EQ(first_snapshot.find_key(id), second_snapshot.find_key(id));
    }
    EXPECT_FALSE(first_snapshot.find_id("missing"));
    EXPECT_FALSE(first_snapshot.find_key(0));
    EXPECT_FALSE(first_snapshot.find_key(5));
}

TEST(RuntimeKeyIndexTest, FailedRebuildPreservesThePublishedSnapshot) {
    RuntimeKeyIndex index;
    const std::array<std::string_view, 2> initial_keys{"copper", "iron"};
    ASSERT_TRUE(index.rebuild(initial_keys));
    const auto before = index.snapshot();
    ASSERT_TRUE(before.find_id("copper"));

    const std::array<std::string_view, 2> duplicate_keys{"copper", "copper"};
    EXPECT_FALSE(index.rebuild(duplicate_keys));

    const auto after = index.snapshot();
    EXPECT_EQ(after.generation(), before.generation());
    EXPECT_EQ(after.find_id("copper"), before.find_id("copper"));
    EXPECT_EQ(after.find_id("iron"), before.find_id("iron"));
    EXPECT_FALSE(after.find_id("zinc"));
}

TEST(RuntimeKeyIndexTest, CapturedSnapshotSurvivesAPublishedReplacement) {
    RuntimeKeyIndex index;
    const std::array<std::string_view, 2> first_keys{"iron", "zinc"};
    ASSERT_TRUE(index.rebuild(first_keys));
    const auto old_snapshot = index.snapshot();

    const std::array<std::string_view, 2> replacement_keys{"charcoal", "iron"};
    ASSERT_TRUE(index.rebuild(replacement_keys));
    const auto new_snapshot = index.snapshot();

    EXPECT_LT(old_snapshot.generation(), new_snapshot.generation());
    EXPECT_TRUE(old_snapshot.find_id("zinc"));
    EXPECT_FALSE(old_snapshot.find_id("charcoal"));
    EXPECT_TRUE(new_snapshot.find_id("charcoal"));
    EXPECT_FALSE(new_snapshot.find_id("zinc"));
}

}  // namespace

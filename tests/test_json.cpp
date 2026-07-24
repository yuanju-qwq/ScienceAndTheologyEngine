// Tests for the project JSON facade backed by the Zig document adapter.

#include "core/json.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

using snt::core::JsonDocument;
using snt::core::JsonValueType;

TEST(JsonDocumentTest, ParsesObjectsArraysAndScalarValues) {
    auto parsed = JsonDocument::parse(R"({"name":"runtime","count":7,"scale":1.5,"items":[true,"two"]})");
    ASSERT_TRUE(parsed.has_value());
    JsonDocument document = std::move(*parsed);

    auto root_type = document.root().type();
    ASSERT_TRUE(root_type.has_value());
    EXPECT_EQ(*root_type, JsonValueType::kObject);

    auto name = document.root().object_find("name");
    ASSERT_TRUE(name.has_value());
    ASSERT_TRUE(name->has_value());
    auto name_text = name->value().read_string();
    ASSERT_TRUE(name_text.has_value());
    EXPECT_EQ(*name_text, "runtime");

    auto count = document.root().object_find("count");
    ASSERT_TRUE(count.has_value());
    ASSERT_TRUE(count->has_value());
    auto count_value = count->value().read_uint64();
    ASSERT_TRUE(count_value.has_value());
    EXPECT_EQ(*count_value, 7u);

    auto items = document.root().object_find("items");
    ASSERT_TRUE(items.has_value());
    ASSERT_TRUE(items->has_value());
    auto item_count = items->value().array_count();
    ASSERT_TRUE(item_count.has_value());
    EXPECT_EQ(*item_count, 2u);
    auto second_item = items->value().array_get(1u);
    ASSERT_TRUE(second_item.has_value());
    auto second_text = second_item->read_string();
    ASSERT_TRUE(second_text.has_value());
    EXPECT_EQ(*second_text, "two");

    auto missing = document.root().object_find("missing");
    ASSERT_TRUE(missing.has_value());
    EXPECT_FALSE(missing->has_value());
}

TEST(JsonDocumentTest, RejectsMalformedAndMismatchedValues) {
    EXPECT_FALSE(JsonDocument::parse("{ invalid json").has_value());
    EXPECT_FALSE(JsonDocument::parse(R"({"key":1,"key":2})").has_value());

    auto parsed = JsonDocument::parse(R"({"name":"runtime"})");
    ASSERT_TRUE(parsed.has_value());
    JsonDocument document = std::move(*parsed);
    auto name = document.root().object_find("name");
    ASSERT_TRUE(name.has_value());
    ASSERT_TRUE(name->has_value());
    EXPECT_FALSE(name->value().read_bool().has_value());
    EXPECT_FALSE(name->value().array_count().has_value());
}

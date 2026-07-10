// RegistryHub tests -- P7 content ownership and reload transaction contract.

#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "script/registry_hub.h"

namespace {

using snt::script::EventListener;
using snt::script::RecipeDefinition;
using snt::script::RecipeOutputDefinition;
using snt::script::RegistryHub;

RecipeDefinition make_recipe(std::string id, std::string input) {
    RecipeDefinition recipe;
    recipe.id = std::move(id);
    recipe.machine_id = "furnace";
    recipe.input_item_id = std::move(input);
    recipe.outputs = {RecipeOutputDefinition{"iron_ingot", 1}};
    recipe.duration_ticks = 100;
    return recipe;
}

}  // namespace

TEST(RegistryHubTest, ScriptOverrideIsRemovedAndBuiltinFallbackRestored) {
    RegistryHub hub;
    ASSERT_TRUE(hub.register_builtin_recipe(make_recipe("iron", "iron_ore")));
    ASSERT_TRUE(hub.register_script_recipe(42, make_recipe("iron", "rich_iron_ore")));

    ASSERT_NE(hub.find_recipe("iron"), nullptr);
    EXPECT_EQ(hub.find_recipe("iron")->input_item_id, "rich_iron_ore");

    ASSERT_TRUE(hub.unload_script(42));
    ASSERT_NE(hub.find_recipe("iron"), nullptr);
    EXPECT_EQ(hub.find_recipe("iron")->input_item_id, "iron_ore");
}

TEST(RegistryHubTest, RollbackRestoresPreviousScriptContentAndState) {
    RegistryHub hub;
    ASSERT_TRUE(hub.register_builtin_recipe(make_recipe("copper", "copper_ore")));
    ASSERT_TRUE(hub.register_script_recipe(7, make_recipe("copper", "dense_copper_ore")));
    ASSERT_TRUE(hub.set_state(7, "furnace.temperature", "840"));
    ASSERT_TRUE(hub.add_event_listener(EventListener{7, "machine.tick", "tick_copper"}));

    ASSERT_TRUE(hub.begin_reload(7));
    ASSERT_NE(hub.find_recipe("copper"), nullptr);
    EXPECT_EQ(hub.find_recipe("copper")->input_item_id, "copper_ore");
    EXPECT_TRUE(hub.event_listeners("machine.tick").empty());

    ASSERT_TRUE(hub.register_script_recipe(7, make_recipe("copper", "broken_copper_ore")));
    ASSERT_TRUE(hub.add_event_listener(EventListener{7, "machine.tick", "broken_tick"}));
    ASSERT_TRUE(hub.rollback_reload(7));

    ASSERT_NE(hub.find_recipe("copper"), nullptr);
    EXPECT_EQ(hub.find_recipe("copper")->input_item_id, "dense_copper_ore");
    ASSERT_EQ(hub.event_listeners("machine.tick").size(), 1U);
    EXPECT_EQ(hub.event_listeners("machine.tick")[0].callback_id, "tick_copper");
    EXPECT_EQ(hub.get_state(7, "furnace.temperature"), "840");
}

TEST(RegistryHubTest, CommitKeepsReplacementAndRejectsDefinitionCollision) {
    RegistryHub hub;
    ASSERT_TRUE(hub.register_script_recipe(10, make_recipe("bronze", "copper")));
    EXPECT_FALSE(hub.register_script_recipe(11, make_recipe("bronze", "tin")));

    ASSERT_TRUE(hub.begin_reload(10));
    ASSERT_TRUE(hub.register_script_recipe(10, make_recipe("bronze", "copper_dust")));
    ASSERT_TRUE(hub.commit_reload(10));

    ASSERT_NE(hub.find_recipe("bronze"), nullptr);
    EXPECT_EQ(hub.find_recipe("bronze")->input_item_id, "copper_dust");
}

TEST(RegistryHubTest, EventListenersAreStableAndStateIsIsolatedByScript) {
    RegistryHub hub;
    ASSERT_TRUE(hub.add_event_listener(EventListener{9, "block.interact", "zeta"}));
    ASSERT_TRUE(hub.add_event_listener(EventListener{3, "block.interact", "alpha"}));
    EXPECT_FALSE(hub.add_event_listener(EventListener{3, "block.interact", "alpha"}));

    const auto listeners = hub.event_listeners("block.interact");
    ASSERT_EQ(listeners.size(), 2U);
    EXPECT_EQ(listeners[0].script_id, 3U);
    EXPECT_EQ(listeners[1].script_id, 9U);

    ASSERT_TRUE(hub.set_state(3, "mode", "normal"));
    ASSERT_TRUE(hub.set_state(9, "mode", "expert"));
    EXPECT_EQ(hub.get_state(3, "mode"), "normal");
    EXPECT_EQ(hub.get_state(9, "mode"), "expert");
}

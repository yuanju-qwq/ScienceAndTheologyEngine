// Gameplay UI builders for the retained MUI system.
//
// Godot Control-based UI is intentionally not used here. These models and
// builders are engine-native and can be driven by ECS/gameplay systems.

#pragma once

#include "retained_mui.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snt::ui {

struct ItemStackState {
    std::string item_key;
    int32_t count = 0;

    bool empty() const { return item_key.empty() || count <= 0; }
};

struct InventoryState {
    std::vector<ItemStackState> slots;
    int32_t columns = 9;
    int32_t selected_hotbar = 0;
};

struct RecipeIngredientState {
    std::string item_key;
    int32_t count = 0;
};

struct CraftingRecipeState {
    std::string id;
    std::string category;
    std::vector<RecipeIngredientState> inputs;
    ItemStackState output;
};

struct CraftedItemResult {
    bool ok = false;
    ItemStackState output;
    std::string reason;
};

struct PerformanceStats {
    float fps = 0.0f;
    float frame_ms = 0.0f;
    float tps = 0.0f;
    float mspt = 0.0f;
    int32_t job_workers = 0;
};

class PerformanceViewModel {
public:
    PerformanceViewModel();

    void publish(PerformanceStats stats);
    const PerformanceStats& stats() const { return stats_; }

    void set_visible(bool visible);
    void toggle_visible() { set_visible(!visible_); }
    bool visible() const { return visible_; }

    ViewModel& bindings() { return bindings_; }
    const ViewModel& bindings() const { return bindings_; }

private:
    PerformanceStats stats_{};
    bool visible_ = true;
    ViewModel bindings_;
};

class InventoryViewModel {
public:
    explicit InventoryViewModel(InventoryState state = {});

    const InventoryState& state() const { return state_; }
    InventoryState& mutable_state() { return state_; }

    int32_t count_item(std::string_view item_key) const;
    bool remove_item(std::string_view item_key, int32_t count);
    bool add_item(ItemStackState stack);

    ViewModel& bindings() { return bindings_; }
    const ViewModel& bindings() const { return bindings_; }
    void publish();

private:
    InventoryState state_{};
    ViewModel bindings_;
};

class HotbarViewModel {
public:
    explicit HotbarViewModel(InventoryViewModel& inventory);

    int32_t selected_index() const;
    void select(int32_t index);
    const InventoryState& inventory() const;
    ViewModel& bindings() { return bindings_; }

private:
    InventoryViewModel& inventory_;
    ViewModel bindings_;
};

class CraftingViewModel {
public:
    CraftingViewModel(InventoryViewModel& inventory, std::vector<CraftingRecipeState> recipes = {});

    const std::vector<CraftingRecipeState>& recipes() const { return recipes_; }
    void set_recipes(std::vector<CraftingRecipeState> recipes) { recipes_ = std::move(recipes); }
    bool can_craft(const CraftingRecipeState& recipe) const;
    CraftedItemResult craft(std::string_view recipe_id);
    ViewModel& bindings() { return bindings_; }

private:
    InventoryViewModel& inventory_;
    std::vector<CraftingRecipeState> recipes_;
    ViewModel bindings_;
};

enum class GameplayUiScreen : uint8_t {
    None,
    Inventory,
    Crafting,
};

class GameplayUiController {
public:
    GameplayUiController(InventoryViewModel inventory,
                         std::vector<CraftingRecipeState> recipes);

    InventoryViewModel& inventory() { return inventory_; }
    HotbarViewModel& hotbar() { return hotbar_; }
    CraftingViewModel& crafting() { return crafting_; }

    GameplayUiScreen open_screen() const { return open_screen_; }
    bool inventory_open() const { return open_screen_ == GameplayUiScreen::Inventory; }
    bool crafting_open() const { return open_screen_ == GameplayUiScreen::Crafting; }

    void open_inventory();
    void open_crafting();
    void close();
    void toggle_inventory();
    void toggle_crafting();

private:
    InventoryViewModel inventory_;
    HotbarViewModel hotbar_;
    CraftingViewModel crafting_;
    GameplayUiScreen open_screen_ = GameplayUiScreen::None;
};

std::unique_ptr<ViewGroup> build_hotbar_view(const HotbarViewModel& model);
std::unique_ptr<ViewGroup> build_inventory_view(const InventoryViewModel& model);
std::unique_ptr<ViewGroup> build_crafting_view(const CraftingViewModel& model);
std::unique_ptr<ViewGroup> build_performance_panel_view(const PerformanceViewModel& model);

std::unique_ptr<ViewGroup> build_gameplay_ui_root(GameplayUiController& controller,
                                                  Vec2 viewport);

std::vector<CraftingRecipeState> make_p6_demo_recipes();
InventoryState make_p6_demo_inventory();

}  // namespace snt::ui

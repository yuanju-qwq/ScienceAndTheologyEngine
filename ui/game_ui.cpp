#define SNT_LOG_CHANNEL "ui"
#include "game_ui.h"

#include "core/log.h"

#include <algorithm>
#include <cstdio>

namespace snt::ui {

namespace {

constexpr float kSlotSize = 36.0f;
constexpr float kSlotGap = 2.0f;

LayoutParams fixed(float width, float height) {
    LayoutParams lp;
    lp.width = width;
    lp.height = height;
    return lp;
}

std::unique_ptr<TextView> label(std::string id, std::string text, float size = 16.0f) {
    auto view = std::make_unique<TextView>(std::move(id));
    view->set_text(std::move(text));
    TextStyle style;
    style.size_px = size;
    style.color = {230, 236, 245, 255};
    view->set_text_style(style);
    return view;
}

std::string format_float(const char* fmt, float value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, value);
    return buf;
}

std::unique_ptr<SlotView> slot_view(std::string id, const ItemStackState& stack, bool selected) {
    auto slot = std::make_unique<SlotView>(std::move(id));
    slot->set_layout_params(fixed(kSlotSize, kSlotSize));
    slot->set_slot_state({
        .item_key = stack.item_key,
        .count = stack.count,
        .selected = selected,
    });
    return slot;
}

void add_slots_grid(LinearLayout& root, const InventoryState& inventory, int32_t columns) {
    const int32_t safe_columns = std::max(1, columns);
    for (int32_t i = 0; i < static_cast<int32_t>(inventory.slots.size()); i += safe_columns) {
        auto row = std::make_unique<LinearLayout>("inventory_row_" + std::to_string(i / safe_columns));
        row->set_orientation(Orientation::Horizontal);
        row->set_spacing(kSlotGap);
        for (int32_t col = 0; col < safe_columns && i + col < static_cast<int32_t>(inventory.slots.size()); ++col) {
            const int32_t index = i + col;
            row->add_child(slot_view("inventory_slot_" + std::to_string(index),
                                     inventory.slots[index],
                                     index == inventory.selected_hotbar));
        }
        root.add_child(std::move(row));
    }
}

}  // namespace

PerformanceViewModel::PerformanceViewModel() {
    publish({});
    bindings_.set("performance.visible", visible_);
}

void PerformanceViewModel::publish(PerformanceStats stats) {
    stats_ = stats;
    bindings_.set("performance.fps", static_cast<double>(stats_.fps));
    bindings_.set("performance.frame_ms", static_cast<double>(stats_.frame_ms));
    bindings_.set("performance.tps", static_cast<double>(stats_.tps));
    bindings_.set("performance.mspt", static_cast<double>(stats_.mspt));
    bindings_.set("performance.job_workers", static_cast<int64_t>(stats_.job_workers));
}

void PerformanceViewModel::set_visible(bool visible) {
    visible_ = visible;
    bindings_.set("performance.visible", visible_);
}

InventoryViewModel::InventoryViewModel(InventoryState state)
    : state_(std::move(state)) {
    publish();
}

int32_t InventoryViewModel::count_item(std::string_view item_key) const {
    int32_t total = 0;
    for (const auto& slot : state_.slots) {
        if (slot.item_key == item_key) {
            total += slot.count;
        }
    }
    return total;
}

bool InventoryViewModel::remove_item(std::string_view item_key, int32_t count) {
    if (count <= 0) return true;
    if (count_item(item_key) < count) return false;

    int32_t remaining = count;
    for (auto& slot : state_.slots) {
        if (slot.item_key != item_key || slot.count <= 0) continue;
        const int32_t take = std::min(slot.count, remaining);
        slot.count -= take;
        remaining -= take;
        if (slot.count <= 0) {
            slot = {};
        }
        if (remaining <= 0) break;
    }
    publish();
    return true;
}

bool InventoryViewModel::add_item(ItemStackState stack) {
    if (stack.empty()) return true;

    for (auto& slot : state_.slots) {
        if (slot.item_key == stack.item_key) {
            slot.count += stack.count;
            publish();
            return true;
        }
    }
    for (auto& slot : state_.slots) {
        if (slot.empty()) {
            slot = std::move(stack);
            publish();
            return true;
        }
    }
    SNT_LOG_WARN("Inventory is full; could not add item '%s'", stack.item_key.c_str());
    return false;
}

void InventoryViewModel::publish() {
    bindings_.set("inventory.slot_count", static_cast<int64_t>(state_.slots.size()));
    bindings_.set("inventory.selected_hotbar", static_cast<int64_t>(state_.selected_hotbar));
}

HotbarViewModel::HotbarViewModel(InventoryViewModel& inventory)
    : inventory_(inventory) {}

int32_t HotbarViewModel::selected_index() const {
    return inventory_.state().selected_hotbar;
}

void HotbarViewModel::select(int32_t index) {
    auto& inv = inventory_.mutable_state();
    if (inv.slots.empty()) return;
    inv.selected_hotbar = std::clamp(index, 0, std::min(8, static_cast<int32_t>(inv.slots.size()) - 1));
    inventory_.publish();
    bindings_.set("hotbar.selected", static_cast<int64_t>(inv.selected_hotbar));
}

const InventoryState& HotbarViewModel::inventory() const {
    return inventory_.state();
}

CraftingViewModel::CraftingViewModel(InventoryViewModel& inventory,
                                     std::vector<CraftingRecipeState> recipes)
    : inventory_(inventory),
      recipes_(std::move(recipes)) {
    bindings_.set("crafting.recipe_count", static_cast<int64_t>(recipes_.size()));
    bindings_.set_command("craft", [this](const BindingValue& payload) {
        if (const auto* recipe_id = std::get_if<std::string>(&payload)) {
            craft(*recipe_id);
        }
    });
}

bool CraftingViewModel::can_craft(const CraftingRecipeState& recipe) const {
    if (recipe.output.empty()) return false;
    for (const auto& input : recipe.inputs) {
        if (input.item_key.empty() || input.count <= 0) return false;
        if (inventory_.count_item(input.item_key) < input.count) return false;
    }
    return true;
}

CraftedItemResult CraftingViewModel::craft(std::string_view recipe_id) {
    auto it = std::find_if(recipes_.begin(), recipes_.end(),
                           [&](const CraftingRecipeState& r) { return r.id == recipe_id; });
    if (it == recipes_.end()) {
        return {.ok = false, .reason = "recipe not found"};
    }
    if (!can_craft(*it)) {
        return {.ok = false, .reason = "missing inputs"};
    }

    for (const auto& input : it->inputs) {
        if (!inventory_.remove_item(input.item_key, input.count)) {
            return {.ok = false, .reason = "input removal failed"};
        }
    }
    if (!inventory_.add_item(it->output)) {
        return {.ok = false, .reason = "output inventory full"};
    }

    bindings_.set("crafting.last_crafted", it->output.item_key);
    return {.ok = true, .output = it->output};
}

GameplayUiController::GameplayUiController(InventoryViewModel inventory,
                                           std::vector<CraftingRecipeState> recipes)
    : inventory_(std::move(inventory)),
      hotbar_(inventory_),
      crafting_(inventory_, std::move(recipes)) {}

void GameplayUiController::open_inventory() {
    open_screen_ = GameplayUiScreen::Inventory;
}

void GameplayUiController::open_crafting() {
    open_screen_ = GameplayUiScreen::Crafting;
}

void GameplayUiController::close() {
    open_screen_ = GameplayUiScreen::None;
}

void GameplayUiController::toggle_inventory() {
    open_screen_ = inventory_open() ? GameplayUiScreen::None : GameplayUiScreen::Inventory;
}

void GameplayUiController::toggle_crafting() {
    open_screen_ = crafting_open() ? GameplayUiScreen::None : GameplayUiScreen::Crafting;
}

std::unique_ptr<ViewGroup> build_hotbar_view(const HotbarViewModel& model) {
    auto root = std::make_unique<LinearLayout>("hotbar");
    root->set_orientation(Orientation::Horizontal);
    root->set_spacing(kSlotGap);
    root->set_padding({4, 4, 4, 4});
    root->set_background({12, 15, 20, 205}, 4.0f);

    const auto& inv = model.inventory();
    const int32_t limit = std::min<int32_t>(9, static_cast<int32_t>(inv.slots.size()));
    for (int32_t i = 0; i < limit; ++i) {
        root->add_child(slot_view("hotbar_slot_" + std::to_string(i),
                                  inv.slots[i],
                                  i == model.selected_index()));
    }
    return root;
}

std::unique_ptr<ViewGroup> build_inventory_view(const InventoryViewModel& model) {
    auto panel = std::make_unique<LinearLayout>("inventory_panel");
    panel->set_orientation(Orientation::Vertical);
    panel->set_spacing(8.0f);
    panel->set_padding({10, 10, 10, 10});
    panel->set_background({13, 15, 21, 235}, 6.0f);
    panel->set_layout_params(fixed(430.0f, 250.0f));

    auto title = label("inventory_title", "背包 Inventory 🎒", 18.0f);
    title->set_layout_params(fixed(300.0f, 28.0f));
    panel->add_child(std::move(title));

    add_slots_grid(*panel, model.state(), std::max(1, model.state().columns));
    return panel;
}

std::unique_ptr<ViewGroup> build_crafting_view(const CraftingViewModel& model) {
    auto panel = std::make_unique<LinearLayout>("crafting_panel");
    panel->set_orientation(Orientation::Vertical);
    panel->set_spacing(8.0f);
    panel->set_padding({10, 10, 10, 10});
    panel->set_background({14, 16, 22, 238}, 6.0f);
    panel->set_layout_params(fixed(520.0f, 310.0f));

    auto title = label("crafting_title", "合成 Crafting ⚒", 18.0f);
    title->set_layout_params(fixed(320.0f, 28.0f));
    panel->add_child(std::move(title));

    for (const auto& recipe : model.recipes()) {
        auto row = std::make_unique<LinearLayout>("recipe_" + recipe.id);
        row->set_orientation(Orientation::Horizontal);
        row->set_spacing(8.0f);
        row->set_layout_params(fixed(480.0f, 48.0f));

        row->add_child(slot_view("recipe_output_" + recipe.id, recipe.output, false));

        std::string text = recipe.output.item_key + " x" + std::to_string(recipe.output.count);
        text += model.can_craft(recipe) ? "  可合成 ✅" : "  材料不足 ❌";
        auto name = label("recipe_label_" + recipe.id, text, 14.0f);
        name->set_layout_params(fixed(280.0f, 36.0f));
        row->add_child(std::move(name));

        auto craft = std::make_unique<Button>("recipe_button_" + recipe.id);
        craft->set_text("合成");
        craft->set_command("craft");
        craft->set_layout_params(fixed(72.0f, 32.0f));
        row->add_child(std::move(craft));

        panel->add_child(std::move(row));
    }

    return panel;
}

std::unique_ptr<ViewGroup> build_performance_panel_view(const PerformanceViewModel& model) {
    auto panel = std::make_unique<LinearLayout>("performance_panel");
    panel->set_orientation(Orientation::Vertical);
    panel->set_spacing(3.0f);
    panel->set_padding({8, 8, 8, 8});
    panel->set_background({10, 12, 16, 210}, 5.0f);
    panel->set_layout_params(fixed(230.0f, 116.0f));
    panel->layout_params().margin.left = 12.0f;
    panel->layout_params().margin.top = 12.0f;

    const auto& s = model.stats();

    auto title = label("performance_title", "性能 Performance", 14.0f);
    title->set_layout_params(fixed(210.0f, 20.0f));
    panel->add_child(std::move(title));

    auto fps = label("performance_fps",
                     "FPS  " + format_float("%.1f", s.fps) +
                         "   Frame " + format_float("%.2f ms", s.frame_ms),
                     12.0f);
    fps->set_layout_params(fixed(210.0f, 18.0f));
    panel->add_child(std::move(fps));

    auto tps = label("performance_tps",
                     "TPS  " + format_float("%.1f", s.tps) +
                         "   MSPT " + format_float("%.2f", s.mspt),
                     12.0f);
    tps->set_layout_params(fixed(210.0f, 18.0f));
    panel->add_child(std::move(tps));

    auto jobs = label("performance_jobs",
                      "Job Workers  " + std::to_string(s.job_workers),
                      12.0f);
    jobs->set_layout_params(fixed(210.0f, 18.0f));
    panel->add_child(std::move(jobs));

    const float fps_fraction = std::clamp(s.fps / 120.0f, 0.0f, 1.0f);
    auto bar_bg = std::make_unique<View>("performance_fps_bar_bg");
    bar_bg->set_layout_params(fixed(210.0f, 5.0f));
    bar_bg->set_background({38, 44, 55, 230}, 2.0f);
    panel->add_child(std::move(bar_bg));

    auto bar = std::make_unique<View>("performance_fps_bar");
    bar->set_layout_params(fixed(std::max(2.0f, 210.0f * fps_fraction), 5.0f));
    bar->set_background({86, 170, 120, 245}, 2.0f);
    panel->add_child(std::move(bar));

    return panel;
}

std::unique_ptr<ViewGroup> build_gameplay_ui_root(GameplayUiController& controller,
                                                  Vec2 viewport) {
    auto root = std::make_unique<FrameLayout>("gameplay_ui_root");
    root->set_layout_params(fixed(viewport.x, viewport.y));

    auto hotbar = build_hotbar_view(controller.hotbar());
    hotbar->set_layout_params(fixed(9.0f * kSlotSize + 8.0f * kSlotGap + 8.0f,
                                    kSlotSize + 8.0f));
    hotbar->layout_params().margin.left = (viewport.x - hotbar->layout_params().width) * 0.5f;
    hotbar->layout_params().margin.top = viewport.y - hotbar->layout_params().height - 18.0f;
    root->add_child(std::move(hotbar));

    if (controller.inventory_open()) {
        auto inventory = build_inventory_view(controller.inventory());
        inventory->layout_params().margin.left = (viewport.x - inventory->layout_params().width) * 0.5f;
        inventory->layout_params().margin.top = (viewport.y - inventory->layout_params().height) * 0.5f;
        root->add_child(std::move(inventory));
    } else if (controller.crafting_open()) {
        auto crafting = build_crafting_view(controller.crafting());
        crafting->layout_params().margin.left = (viewport.x - crafting->layout_params().width) * 0.5f;
        crafting->layout_params().margin.top = (viewport.y - crafting->layout_params().height) * 0.5f;
        root->add_child(std::move(crafting));
    }

    return root;
}

std::vector<CraftingRecipeState> make_p6_demo_recipes() {
    return {
        {
            .id = "craft_workbench",
            .category = "tools",
            .inputs = {{"plank.oak", 4}},
            .output = {"workbench", 1},
        },
        {
            .id = "craft_torch",
            .category = "misc",
            .inputs = {{"material.coal", 1}, {"stick", 1}},
            .output = {"torch", 4},
        },
    };
}

InventoryState make_p6_demo_inventory() {
    InventoryState state;
    state.columns = 9;
    state.selected_hotbar = 0;
    state.slots.resize(36);
    state.slots[0] = {"plank.oak", 8};
    state.slots[1] = {"material.coal", 2};
    state.slots[2] = {"stick", 4};
    return state;
}

}  // namespace snt::ui

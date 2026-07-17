// Engine-owned packed UI scene format and retained-MUI instantiator.
//
// UiWidgetTree is the immutable UI intermediate representation. Editor
// tooling, JSON PackedScene assets, C++ dynamic builders, and future Mod
// bindings all produce this same tree; only mounting creates retained View
// objects. UiPackedScene is intentionally not a Godot PackedScene and
// contains no Godot object state.

#pragma once

#include "core/expected.h"
#include "ui/retained_mui_screen_stack.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace snt::ui {

enum class UiWidgetType : uint8_t {
    View,
    Flex,
    Grid,
    Frame,
    Text,
    Button,
    TextInput,
    TextEditor,
    Checkbox,
    Slider,
    VirtualList,
    Modal,
    Tooltip,
    Image,
    NineSlice,
    Slot,
    Scroll,
};

// Layout fields are shared by resource-authored and dynamically generated
// widgets. Fields that are irrelevant to a widget kind are ignored by the
// instantiator after validation, keeping the serialized shape stable as new
// widget kinds are added.
struct UiWidgetLayout {
    LayoutParams params{};
    Insets padding{};
    Orientation orientation = Orientation::Vertical;
    FlexJustify justify = FlexJustify::Start;
    FlexAlign align = FlexAlign::Stretch;
    float spacing = 0.0f;
    int32_t columns = 1;
    float column_spacing = 0.0f;
    float row_spacing = 0.0f;
    ScrollAxis scroll_axis = ScrollAxis::Vertical;
    float scroll_step = 36.0f;
};

struct UiWidgetSlotState {
    std::string item_key;
    int32_t count = 0;
    bool selected = false;
};

// One immutable template node. `action_id` is declarative: the screen host
// decides how to dispatch it, so packed resources never retain raw script or
// native function pointers.
struct UiWidgetTemplate {
    UiWidgetType type = UiWidgetType::View;
    std::string id;
    UiWidgetLayout layout{};
    Visibility visibility = Visibility::Visible;
    bool enabled = true;
    // Unspecified keeps each concrete widget's semantic default. For example,
    // Button and SlotView begin interactive while a plain View does not.
    std::optional<bool> hit_test_visible;
    std::optional<bool> focusable;
    std::optional<Color> background;
    float background_radius = 0.0f;
    std::string text;
    // P2 control state is declarative. Runtime interaction stays in the
    // retained instance, while screen hosts choose how to react to action_id.
    std::string placeholder;
    bool password = false;
    int32_t max_text_bytes = 4096;
    int32_t min_text_lines = 3;
    bool checked = false;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float step = 0.0f;
    float value = 0.0f;
    int32_t virtual_item_count = 0;
    float virtual_item_extent = 32.0f;
    Color modal_backdrop{0, 0, 0, 150};
    bool dismiss_on_backdrop = false;
    TextStyle text_style{};
    std::string image_key;
    Color image_tint{255, 255, 255, 255};
    Insets nine_slice_borders{};
    UiWidgetSlotState slot{};
    std::string action_id;
    std::vector<UiWidgetTemplate> children;
};

struct UiWidgetTree {
    UiWidgetTemplate root;
};

// Serialized resource envelope. Keeping the version at this boundary lets
// dynamic builders work with the stable tree shape without pretending their
// in-memory output originated from an asset file.
struct UiPackedScene {
    static constexpr uint32_t kCurrentFormatVersion = 3;

    uint32_t format_version = kCurrentFormatVersion;
    UiWidgetTree tree;
};

// A host maps a declarative action name onto a native command, a script
// callback, or a network request. It runs only on explicit widget activation.
using UiWidgetActionDispatcher = UiActionDispatcher;

struct UiWidgetBuildContext {
    UiWidgetActionDispatcher dispatch_action;
};

// Validates limits, widget/type combinations, layout values, and globally
// stable node IDs before a dynamic tree can enter the layer stack.
[[nodiscard]] snt::core::Expected<void> validate_ui_widget_tree(
    const UiWidgetTree& tree);
[[nodiscard]] snt::core::Expected<void> validate_ui_packed_scene(
    const UiPackedScene& scene);

// Parse the portable JSON source form used by editor exports and Mod assets.
// I/O stays optional so callers can load scenes through package, archive, or
// network asset sources without changing the resource contract.
[[nodiscard]] snt::core::Expected<UiPackedScene> parse_ui_packed_scene_json(
    std::string_view source,
    std::string_view source_identity = {});
[[nodiscard]] snt::core::Expected<UiPackedScene> load_ui_packed_scene_file(
    const std::filesystem::path& path);

// Lower one validated tree into a retained-MUI View tree. UiLayerStack mounts
// this only once per visible screen, keeping layout, focus, pointer, and
// widget-local state in the retained runtime.
[[nodiscard]] snt::core::Expected<std::unique_ptr<View>> instantiate_ui_widget_tree(
    const UiWidgetTree& tree,
    UiWidgetBuildContext context = {});
[[nodiscard]] snt::core::Expected<std::unique_ptr<View>> instantiate_ui_packed_scene(
    const UiPackedScene& scene,
    UiWidgetBuildContext context = {});

// Factory adapters for the shared WidgetTree boundary. A dynamic Retained-MUI
// builder and a serialized UiPackedScene therefore follow the same mount
// path: instantiate exactly once, then retain the concrete View tree until
// its owner replaces, hides, or unloads the screen. Declarative actions are
// supplied by UiScreenMountContext at mount time.
[[nodiscard]] UiScreenFactory make_ui_widget_tree_factory(
    std::shared_ptr<const UiWidgetTree> tree);
[[nodiscard]] UiScreenFactory make_ui_packed_scene_factory(
    std::shared_ptr<const UiPackedScene> scene);

// Dynamic retained-MUI authoring API. Game code and native Mods assemble the
// exact UiWidgetTree that resource importers deserialize.
class UiWidgetTreeBuilder final {
public:
    explicit UiWidgetTreeBuilder(UiWidgetType root_type, std::string root_id);

    UiWidgetTemplate& root() noexcept { return tree_.root; }
    UiWidgetTemplate& add_child(UiWidgetTemplate& parent, UiWidgetTemplate child);

    [[nodiscard]] UiWidgetTree finish() && { return std::move(tree_); }

private:
    UiWidgetTree tree_;
};

}  // namespace snt::ui

// Engine-owned packed UI scene implementation.

#define SNT_LOG_CHANNEL "ui.packed_scene"
#include "ui_packed_scene.h"

#include <nlohmann/json.hpp>

#include "core/error.h"
#include "core/log.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace snt::ui {
namespace {

using json = nlohmann::json;

constexpr size_t kMaximumWidgetDepth = 64;
constexpr size_t kMaximumWidgetCount = 4096;

snt::core::Error invalid_argument(std::string message) {
    return snt::core::Error{snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

bool finite(float value) {
    return std::isfinite(value);
}

snt::core::Expected<float> parse_float(const json& value, std::string_view path) {
    if (!value.is_number()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                std::string(path) + " must be a number"};
    }
    const float result = value.get<float>();
    if (!finite(result)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                std::string(path) + " must be finite"};
    }
    return result;
}

snt::core::Expected<void> read_optional_float(const json& object,
                                               const char* name,
                                               float& destination,
                                               std::string_view path) {
    const auto found = object.find(name);
    if (found == object.end()) return {};
    auto value = parse_float(*found, std::string(path) + "." + name);
    if (!value) return value.error();
    destination = *value;
    return {};
}

snt::core::Expected<void> read_optional_int(const json& object,
                                             const char* name,
                                             int32_t& destination,
                                             std::string_view path) {
    const auto found = object.find(name);
    if (found == object.end()) return {};
    if (!found->is_number_integer() && !found->is_number_unsigned()) {
        return invalid_argument(std::string(path) + "." + name + " must be an integer");
    }
    if (found->is_number_unsigned() &&
        found->get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        return invalid_argument(std::string(path) + "." + name + " is outside int32 range");
    }
    const int64_t value = found->is_number_unsigned()
        ? static_cast<int64_t>(found->get<uint64_t>())
        : found->get<int64_t>();
    if (value < std::numeric_limits<int32_t>::min() ||
        value > std::numeric_limits<int32_t>::max()) {
        return invalid_argument(std::string(path) + "." + name + " is outside int32 range");
    }
    destination = static_cast<int32_t>(value);
    return {};
}

snt::core::Expected<void> read_optional_bool(const json& object,
                                              const char* name,
                                              bool& destination,
                                              std::string_view path) {
    const auto found = object.find(name);
    if (found == object.end()) return {};
    if (!found->is_boolean()) {
        return invalid_argument(std::string(path) + "." + name + " must be a boolean");
    }
    destination = found->get<bool>();
    return {};
}

snt::core::Expected<void> read_optional_string(const json& object,
                                                const char* name,
                                                std::string& destination,
                                                std::string_view path) {
    const auto found = object.find(name);
    if (found == object.end()) return {};
    if (!found->is_string()) {
        return invalid_argument(std::string(path) + "." + name + " must be a string");
    }
    destination = found->get<std::string>();
    return {};
}

snt::core::Expected<Color> parse_color(const json& value, std::string_view path) {
    if (!value.is_array() || (value.size() != 3 && value.size() != 4)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                std::string(path) + " must be an RGB or RGBA integer array"};
    }

    Color color{0, 0, 0, 255};
    uint8_t* channels[] = {&color.r, &color.g, &color.b, &color.a};
    for (size_t index = 0; index < value.size(); ++index) {
        if (!value[index].is_number_integer() && !value[index].is_number_unsigned()) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    std::string(path) + " color channels must be integers"};
        }
        const int64_t channel = value[index].is_number_unsigned()
            ? static_cast<int64_t>(value[index].get<uint64_t>())
            : value[index].get<int64_t>();
        if (channel < 0 || channel > 255) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    std::string(path) + " color channels must be within [0, 255]"};
        }
        *channels[index] = static_cast<uint8_t>(channel);
    }
    return color;
}

snt::core::Expected<Insets> parse_insets(const json& value, std::string_view path) {
    if (!value.is_array() || value.size() != 4) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                std::string(path) + " must be [left, top, right, bottom]"};
    }
    auto left = parse_float(value[0], std::string(path) + "[0]");
    auto top = parse_float(value[1], std::string(path) + "[1]");
    auto right = parse_float(value[2], std::string(path) + "[2]");
    auto bottom = parse_float(value[3], std::string(path) + "[3]");
    if (!left) return left.error();
    if (!top) return top.error();
    if (!right) return right.error();
    if (!bottom) return bottom.error();
    return Insets{*left, *top, *right, *bottom};
}

snt::core::Expected<UiWidgetType> parse_widget_type(std::string_view value,
                                                     std::string_view path) {
    if (value == "view") return UiWidgetType::View;
    if (value == "flex") return UiWidgetType::Flex;
    if (value == "grid") return UiWidgetType::Grid;
    if (value == "frame") return UiWidgetType::Frame;
    if (value == "text") return UiWidgetType::Text;
    if (value == "button") return UiWidgetType::Button;
    if (value == "text_input") return UiWidgetType::TextInput;
    if (value == "text_editor") return UiWidgetType::TextEditor;
    if (value == "checkbox") return UiWidgetType::Checkbox;
    if (value == "slider") return UiWidgetType::Slider;
    if (value == "virtual_list") return UiWidgetType::VirtualList;
    if (value == "modal") return UiWidgetType::Modal;
    if (value == "tooltip") return UiWidgetType::Tooltip;
    if (value == "image") return UiWidgetType::Image;
    if (value == "nine_slice") return UiWidgetType::NineSlice;
    if (value == "slot") return UiWidgetType::Slot;
    if (value == "scroll") return UiWidgetType::Scroll;
    return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                            std::string(path) + " has an unknown widget type: " + std::string(value)};
}

snt::core::Expected<Orientation> parse_orientation(std::string_view value,
                                                    std::string_view path) {
    if (value == "vertical") return Orientation::Vertical;
    if (value == "horizontal") return Orientation::Horizontal;
    return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                            std::string(path) + " must be 'vertical' or 'horizontal'"};
}

snt::core::Expected<FlexJustify> parse_flex_justify(std::string_view value,
                                                     std::string_view path) {
    if (value == "start") return FlexJustify::Start;
    if (value == "center") return FlexJustify::Center;
    if (value == "end") return FlexJustify::End;
    if (value == "space_between") return FlexJustify::SpaceBetween;
    if (value == "space_around") return FlexJustify::SpaceAround;
    if (value == "space_evenly") return FlexJustify::SpaceEvenly;
    return invalid_argument(std::string(path) + " has an unknown flex justify value");
}

snt::core::Expected<FlexAlign> parse_flex_align(std::string_view value,
                                                 std::string_view path) {
    if (value == "start") return FlexAlign::Start;
    if (value == "center") return FlexAlign::Center;
    if (value == "end") return FlexAlign::End;
    if (value == "stretch") return FlexAlign::Stretch;
    return invalid_argument(std::string(path) + " has an unknown flex align value");
}

snt::core::Expected<ScrollAxis> parse_scroll_axis(std::string_view value,
                                                   std::string_view path) {
    if (value == "vertical") return ScrollAxis::Vertical;
    if (value == "horizontal") return ScrollAxis::Horizontal;
    if (value == "both") return ScrollAxis::Both;
    return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                            std::string(path) + " must be 'vertical', 'horizontal', or 'both'"};
}

snt::core::Expected<Visibility> parse_visibility(std::string_view value,
                                                  std::string_view path) {
    if (value == "visible") return Visibility::Visible;
    if (value == "hidden") return Visibility::Hidden;
    if (value == "gone") return Visibility::Gone;
    return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                            std::string(path) + " has an unknown visibility value"};
}

snt::core::Expected<void> parse_layout(const json& value,
                                        UiWidgetLayout& layout,
                                        std::string_view path) {
    if (!value.is_object()) {
        return invalid_argument(std::string(path) + " must be an object");
    }
    if (auto result = read_optional_float(value, "width", layout.params.width, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "height", layout.params.height, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "weight", layout.params.weight, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "spacing", layout.spacing, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_int(value, "columns", layout.columns, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "column_spacing", layout.column_spacing, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "row_spacing", layout.row_spacing, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "scroll_step", layout.scroll_step, path); !result) {
        return result.error();
    }

    if (const auto margin = value.find("margin"); margin != value.end()) {
        auto parsed = parse_insets(*margin, std::string(path) + ".margin");
        if (!parsed) return parsed.error();
        layout.params.margin = *parsed;
    }
    if (const auto padding = value.find("padding"); padding != value.end()) {
        auto parsed = parse_insets(*padding, std::string(path) + ".padding");
        if (!parsed) return parsed.error();
        layout.padding = *parsed;
    }
    if (const auto orientation = value.find("orientation"); orientation != value.end()) {
        if (!orientation->is_string()) {
            return invalid_argument(std::string(path) + ".orientation must be a string");
        }
        auto parsed = parse_orientation(orientation->get<std::string>(),
                                        std::string(path) + ".orientation");
        if (!parsed) return parsed.error();
        layout.orientation = *parsed;
    }
    if (const auto justify = value.find("justify"); justify != value.end()) {
        if (!justify->is_string()) {
            return invalid_argument(std::string(path) + ".justify must be a string");
        }
        auto parsed = parse_flex_justify(justify->get<std::string>(),
                                         std::string(path) + ".justify");
        if (!parsed) return parsed.error();
        layout.justify = *parsed;
    }
    if (const auto align = value.find("align"); align != value.end()) {
        if (!align->is_string()) {
            return invalid_argument(std::string(path) + ".align must be a string");
        }
        auto parsed = parse_flex_align(align->get<std::string>(),
                                       std::string(path) + ".align");
        if (!parsed) return parsed.error();
        layout.align = *parsed;
    }
    if (const auto scroll_axis = value.find("scroll_axis"); scroll_axis != value.end()) {
        if (!scroll_axis->is_string()) {
            return invalid_argument(std::string(path) + ".scroll_axis must be a string");
        }
        auto parsed = parse_scroll_axis(scroll_axis->get<std::string>(),
                                        std::string(path) + ".scroll_axis");
        if (!parsed) return parsed.error();
        layout.scroll_axis = *parsed;
    }
    return {};
}

snt::core::Expected<UiWidgetTemplate> parse_node(const json& value,
                                                  std::string path,
                                                  size_t depth) {
    if (depth > kMaximumWidgetDepth) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                path + " exceeds the maximum widget depth"};
    }
    if (!value.is_object()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                path + " must be an object"};
    }
    const auto type = value.find("type");
    const auto id = value.find("id");
    if (type == value.end() || !type->is_string()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                path + ".type must be a string"};
    }
    if (id == value.end() || !id->is_string()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                path + ".id must be a string"};
    }

    auto widget_type = parse_widget_type(type->get<std::string>(), path + ".type");
    if (!widget_type) return widget_type.error();

    UiWidgetTemplate node;
    node.type = *widget_type;
    node.id = id->get<std::string>();
    if (const auto layout = value.find("layout"); layout != value.end()) {
        if (auto result = parse_layout(*layout, node.layout, path + ".layout"); !result) {
            return result.error();
        }
    }
    if (auto result = read_optional_bool(value, "enabled", node.enabled, path); !result) {
        return result.error();
    }
    if (const auto hit_test_visible = value.find("hit_test_visible");
        hit_test_visible != value.end()) {
        if (!hit_test_visible->is_boolean()) {
            return invalid_argument(path + ".hit_test_visible must be a boolean");
        }
        node.hit_test_visible = hit_test_visible->get<bool>();
    }
    if (const auto focusable = value.find("focusable"); focusable != value.end()) {
        if (!focusable->is_boolean()) {
            return invalid_argument(path + ".focusable must be a boolean");
        }
        node.focusable = focusable->get<bool>();
    }
    if (auto result = read_optional_string(value, "text", node.text, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_string(value, "placeholder", node.placeholder, path);
        !result) {
        return result.error();
    }
    if (auto result = read_optional_bool(value, "password", node.password, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_int(value, "max_text_bytes", node.max_text_bytes, path);
        !result) {
        return result.error();
    }
    if (auto result = read_optional_int(value, "min_text_lines", node.min_text_lines, path);
        !result) {
        return result.error();
    }
    if (auto result = read_optional_bool(value, "checked", node.checked, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "minimum", node.minimum, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "maximum", node.maximum, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "step", node.step, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "value", node.value, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_int(value, "virtual_item_count",
                                        node.virtual_item_count, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_float(value, "virtual_item_extent",
                                          node.virtual_item_extent, path); !result) {
        return result.error();
    }
    if (const auto backdrop = value.find("modal_backdrop"); backdrop != value.end()) {
        auto parsed = parse_color(*backdrop, path + ".modal_backdrop");
        if (!parsed) return parsed.error();
        node.modal_backdrop = *parsed;
    }
    if (auto result = read_optional_bool(value, "dismiss_on_backdrop",
                                         node.dismiss_on_backdrop, path); !result) {
        return result.error();
    }
    if (auto result = read_optional_string(value, "image", node.image_key, path); !result) {
        return result.error();
    }
    if (const auto borders = value.find("nine_slice"); borders != value.end()) {
        auto parsed = parse_insets(*borders, path + ".nine_slice");
        if (!parsed) return parsed.error();
        node.nine_slice_borders = *parsed;
    }
    if (auto result = read_optional_string(value, "action", node.action_id, path); !result) {
        return result.error();
    }

    if (const auto visibility = value.find("visibility"); visibility != value.end()) {
        if (!visibility->is_string()) {
            return invalid_argument(path + ".visibility must be a string");
        }
        auto parsed = parse_visibility(visibility->get<std::string>(), path + ".visibility");
        if (!parsed) return parsed.error();
        node.visibility = *parsed;
    }
    if (const auto background = value.find("background"); background != value.end()) {
        if (!background->is_object()) {
            return invalid_argument(path + ".background must be an object");
        }
        const auto color = background->find("color");
        if (color == background->end()) {
            return invalid_argument(path + ".background.color is required");
        }
        auto parsed = parse_color(*color, path + ".background.color");
        if (!parsed) return parsed.error();
        node.background = *parsed;
        if (auto result = read_optional_float(*background, "radius", node.background_radius,
                                              path + ".background"); !result) {
            return result.error();
        }
    }
    if (const auto text_style = value.find("text_style"); text_style != value.end()) {
        if (!text_style->is_object()) {
            return invalid_argument(path + ".text_style must be an object");
        }
        if (auto result = read_optional_float(*text_style, "size", node.text_style.size_px,
                                              path + ".text_style"); !result) {
            return result.error();
        }
        if (auto result = read_optional_bool(*text_style, "sdf", node.text_style.sdf,
                                             path + ".text_style"); !result) {
            return result.error();
        }
        if (auto result = read_optional_bool(*text_style, "emoji", node.text_style.emoji,
                                             path + ".text_style"); !result) {
            return result.error();
        }
        if (const auto color = text_style->find("color"); color != text_style->end()) {
            auto parsed = parse_color(*color, path + ".text_style.color");
            if (!parsed) return parsed.error();
            node.text_style.color = *parsed;
        }
    }
    if (const auto tint = value.find("image_tint"); tint != value.end()) {
        auto parsed = parse_color(*tint, path + ".image_tint");
        if (!parsed) return parsed.error();
        node.image_tint = *parsed;
    }
    if (const auto slot = value.find("slot"); slot != value.end()) {
        if (!slot->is_object()) return invalid_argument(path + ".slot must be an object");
        if (auto result = read_optional_string(*slot, "item", node.slot.item_key,
                                               path + ".slot"); !result) {
            return result.error();
        }
        if (auto result = read_optional_int(*slot, "count", node.slot.count, path + ".slot");
            !result) {
            return result.error();
        }
        if (auto result = read_optional_bool(*slot, "selected", node.slot.selected, path + ".slot");
            !result) {
            return result.error();
        }
    }
    if (const auto children = value.find("children"); children != value.end()) {
        if (!children->is_array()) return invalid_argument(path + ".children must be an array");
        node.children.reserve(children->size());
        for (size_t index = 0; index < children->size(); ++index) {
            auto child = parse_node((*children)[index],
                                    path + ".children[" + std::to_string(index) + "]",
                                    depth + 1);
            if (!child) return child.error();
            node.children.push_back(std::move(*child));
        }
    }
    return node;
}

snt::core::Expected<void> validate_finite(float value, std::string_view path) {
    if (finite(value)) return {};
    return invalid_argument(std::string(path) + " must be finite");
}

snt::core::Expected<void> validate_insets(const Insets& value, std::string_view path) {
    if (auto result = validate_finite(value.left, std::string(path) + ".left"); !result) {
        return result.error();
    }
    if (auto result = validate_finite(value.top, std::string(path) + ".top"); !result) {
        return result.error();
    }
    if (auto result = validate_finite(value.right, std::string(path) + ".right"); !result) {
        return result.error();
    }
    return validate_finite(value.bottom, std::string(path) + ".bottom");
}

snt::core::Expected<void> validate_node(const UiWidgetTemplate& node,
                                        std::string path,
                                        size_t depth,
                                        size_t& node_count,
                                        std::unordered_set<std::string>& ids) {
    if (depth > kMaximumWidgetDepth) return invalid_argument(path + " exceeds the maximum widget depth");
    if (++node_count > kMaximumWidgetCount) return invalid_argument("UiPackedScene exceeds the maximum widget count");
    if (node.id.empty()) return invalid_argument(path + ".id must not be empty");
    if (!ids.emplace(node.id).second) return invalid_argument("UiPackedScene contains duplicate widget id: " + node.id);

    const UiWidgetLayout& layout = node.layout;
    if (auto result = validate_finite(layout.params.width, path + ".layout.width"); !result) {
        return result.error();
    }
    if (auto result = validate_finite(layout.params.height, path + ".layout.height"); !result) {
        return result.error();
    }
    if (auto result = validate_finite(layout.params.weight, path + ".layout.weight"); !result) {
        return result.error();
    }
    if (auto result = validate_insets(layout.params.margin, path + ".layout.margin"); !result) {
        return result.error();
    }
    if (auto result = validate_insets(layout.padding, path + ".layout.padding"); !result) {
        return result.error();
    }
    if (!finite(layout.spacing) || layout.spacing < 0.0f ||
        !finite(layout.column_spacing) || layout.column_spacing < 0.0f ||
        !finite(layout.row_spacing) || layout.row_spacing < 0.0f ||
        !finite(layout.scroll_step) || layout.scroll_step <= 0.0f) {
        return invalid_argument(path + " has an invalid non-negative layout spacing or scroll step");
    }
    if (layout.columns < 1) return invalid_argument(path + ".layout.columns must be positive");
    if (!finite(node.background_radius) || node.background_radius < 0.0f) {
        return invalid_argument(path + ".background radius must be non-negative");
    }
    if (!finite(node.text_style.size_px) || node.text_style.size_px <= 0.0f) {
        return invalid_argument(path + ".text_style.size must be positive");
    }
    if (node.max_text_bytes <= 0 || node.min_text_lines <= 0) {
        return invalid_argument(path + ".max_text_bytes and min_text_lines must be positive");
    }
    if (!finite(node.minimum) || !finite(node.maximum) || !finite(node.step) ||
        !finite(node.value) || node.maximum < node.minimum || node.step < 0.0f) {
        return invalid_argument(path + " has an invalid slider range");
    }
    if (node.virtual_item_count < 0 || !finite(node.virtual_item_extent) ||
        node.virtual_item_extent <= 0.0f) {
        return invalid_argument(path + " has an invalid virtual-list configuration");
    }


    const bool leaf = node.type == UiWidgetType::View || node.type == UiWidgetType::Text ||
                      node.type == UiWidgetType::Button || node.type == UiWidgetType::TextInput ||
                      node.type == UiWidgetType::TextEditor ||
                      node.type == UiWidgetType::Checkbox || node.type == UiWidgetType::Slider ||
                      node.type == UiWidgetType::Image || node.type == UiWidgetType::NineSlice ||
                      node.type == UiWidgetType::Tooltip || node.type == UiWidgetType::Slot;
    if (leaf && !node.children.empty()) {
        return invalid_argument(path + " is a leaf widget and cannot have children");
    }
    if ((node.type == UiWidgetType::Scroll || node.type == UiWidgetType::VirtualList) &&
        node.children.size() > 1) {
        return invalid_argument(path + " scroll and virtual-list widgets accept at most one content child");
    }
    if ((node.type == UiWidgetType::Image || node.type == UiWidgetType::NineSlice) &&
        node.image_key.empty()) {
        return invalid_argument(path + ".image must not be empty for image widgets");
    }
    if (node.type != UiWidgetType::Image && node.type != UiWidgetType::NineSlice &&
        !node.image_key.empty()) {
        return invalid_argument(path + ".image is only valid on image widgets");
    }
    if (node.type != UiWidgetType::NineSlice &&
        (node.nine_slice_borders.left != 0.0f || node.nine_slice_borders.top != 0.0f ||
         node.nine_slice_borders.right != 0.0f || node.nine_slice_borders.bottom != 0.0f)) {
        return invalid_argument(path + ".nine_slice is only valid on nine-slice widgets");
    }
    if (node.type == UiWidgetType::NineSlice &&
        (node.nine_slice_borders.left < 0.0f || node.nine_slice_borders.top < 0.0f ||
         node.nine_slice_borders.right < 0.0f || node.nine_slice_borders.bottom < 0.0f)) {
        return invalid_argument(path + ".nine_slice borders must be non-negative");
    }
    const bool action_capable = node.type == UiWidgetType::Button ||
                                node.type == UiWidgetType::TextInput ||
                                node.type == UiWidgetType::TextEditor ||
                                node.type == UiWidgetType::Checkbox ||
                                node.type == UiWidgetType::Slider || node.type == UiWidgetType::Modal;
    if (!action_capable && !node.action_id.empty()) {
        return invalid_argument(path + ".action is only valid on interactive widgets");
    }
    if (node.type == UiWidgetType::Slot && node.slot.count < 0) {
        return invalid_argument(path + ".slot.count must not be negative");
    }

    for (size_t index = 0; index < node.children.size(); ++index) {
        auto result = validate_node(node.children[index],
                                    path + ".children[" + std::to_string(index) + "]",
                                    depth + 1, node_count, ids);
        if (!result) return result.error();
    }
    return {};
}

void apply_common_view_properties(View& view, const UiWidgetTemplate& node) {
    view.set_layout_params(node.layout.params);
    view.set_visibility(node.visibility);
    view.set_enabled(node.enabled);
    if (node.hit_test_visible) view.set_hit_test_visible(*node.hit_test_visible);
    if (node.focusable) view.set_focusable(*node.focusable);
    if (node.background) view.set_background(*node.background, node.background_radius);
}
void prefix_widget_ids(UiWidgetTemplate& node, std::string_view prefix) {
    node.id = std::string(prefix) + node.id;
    for (UiWidgetTemplate& child : node.children) prefix_widget_ids(child, prefix);
}


snt::core::Expected<std::unique_ptr<View>> instantiate_node(
    const UiWidgetTemplate& node,
    const UiWidgetBuildContext& context) {
    const auto add_children = [&node, &context](ViewGroup& group) -> snt::core::Expected<void> {
        for (const UiWidgetTemplate& child_node : node.children) {
            auto child = instantiate_node(child_node, context);
            if (!child) return child.error();
            group.add_child(std::move(*child));
        }
        return {};
    };

    switch (node.type) {
    case UiWidgetType::View: {
        auto view = std::make_unique<View>(node.id);
        apply_common_view_properties(*view, node);
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Flex: {
        auto view = std::make_unique<FlexLayout>(node.id);
        apply_common_view_properties(*view, node);
        view->set_orientation(node.layout.orientation);
        view->set_justify(node.layout.justify);
        view->set_align(node.layout.align);
        view->set_spacing(node.layout.spacing);
        view->set_padding(node.layout.padding);
        if (auto result = add_children(*view); !result) return result.error();
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Grid: {
        auto view = std::make_unique<GridLayout>(node.id);
        apply_common_view_properties(*view, node);
        view->set_columns(node.layout.columns);
        view->set_column_spacing(node.layout.column_spacing);
        view->set_row_spacing(node.layout.row_spacing);
        view->set_padding(node.layout.padding);
        if (auto result = add_children(*view); !result) return result.error();
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Frame: {
        auto view = std::make_unique<FrameLayout>(node.id);
        apply_common_view_properties(*view, node);
        view->set_padding(node.layout.padding);
        if (auto result = add_children(*view); !result) return result.error();
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Text: {
        auto view = std::make_unique<TextView>(node.id);
        apply_common_view_properties(*view, node);
        view->set_text(node.text);
        view->set_text_style(node.text_style);
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Button: {
        auto view = std::make_unique<Button>(node.id);
        apply_common_view_properties(*view, node);
        view->set_text(node.text);
        view->set_text_style(node.text_style);
        if (!node.action_id.empty() && context.dispatch_action) {
            const std::string action_id = node.action_id;
            const UiWidgetActionDispatcher dispatcher = context.dispatch_action;
            view->set_on_activate([dispatcher, action_id] { dispatcher(action_id); });
        }
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::TextInput: {
        auto view = std::make_unique<TextInput>(node.id);
        apply_common_view_properties(*view, node);
        view->set_text_silently(node.text);
        view->set_text_style(node.text_style);
        view->set_placeholder(node.placeholder);
        view->set_password(node.password);
        view->set_max_bytes(static_cast<size_t>(node.max_text_bytes));
        if (!node.action_id.empty() && context.dispatch_action) {
            const std::string action_id = node.action_id;
            const UiWidgetActionDispatcher dispatcher = context.dispatch_action;
            view->set_on_submit([dispatcher, action_id](std::string_view) { dispatcher(action_id); });
        }
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::TextEditor: {
        auto view = std::make_unique<TextEditor>(node.id);
        apply_common_view_properties(*view, node);
        view->set_text_silently(node.text);
        view->set_text_style(node.text_style);
        view->set_placeholder(node.placeholder);
        view->set_password(node.password);
        view->set_max_bytes(static_cast<size_t>(node.max_text_bytes));
        view->set_min_visible_lines(static_cast<uint32_t>(node.min_text_lines));
        if (!node.action_id.empty() && context.dispatch_action) {
            const std::string action_id = node.action_id;
            const UiWidgetActionDispatcher dispatcher = context.dispatch_action;
            view->set_on_submit([dispatcher, action_id](std::string_view) { dispatcher(action_id); });
        }
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Checkbox: {
        auto view = std::make_unique<Checkbox>(node.id);
        apply_common_view_properties(*view, node);
        view->set_text(node.text);
        view->set_text_style(node.text_style);
        view->set_checked(node.checked);
        if (!node.action_id.empty() && context.dispatch_action) {
            const std::string action_id = node.action_id;
            const UiWidgetActionDispatcher dispatcher = context.dispatch_action;
            view->set_on_change([dispatcher, action_id](bool) { dispatcher(action_id); });
        }
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Slider: {
        auto view = std::make_unique<Slider>(node.id);
        apply_common_view_properties(*view, node);
        view->set_range(node.minimum, node.maximum);
        view->set_step(node.step);
        view->set_value(node.value);
        if (!node.action_id.empty() && context.dispatch_action) {
            const std::string action_id = node.action_id;
            const UiWidgetActionDispatcher dispatcher = context.dispatch_action;
            view->set_on_change([dispatcher, action_id](float) { dispatcher(action_id); });
        }
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::VirtualList: {
        auto view = std::make_unique<VirtualListView>(node.id);
        apply_common_view_properties(*view, node);
        view->set_item_count(static_cast<size_t>(node.virtual_item_count));
        view->set_item_extent(node.virtual_item_extent);
        if (!node.children.empty()) {
            const UiWidgetTemplate item_template = node.children.front();
            const UiWidgetBuildContext item_context = context;
            const std::string item_prefix = node.id + "[";
            view->set_item_builder([item_template, item_context, item_prefix](size_t index)
                                   -> std::unique_ptr<View> {
                UiWidgetTemplate item = item_template;
                prefix_widget_ids(item, item_prefix + std::to_string(index) + "]/");
                auto built = instantiate_node(item, item_context);
                if (!built) {
                    SNT_LOG_ERROR("UiPackedScene virtual-list item mount failed: %s",
                                  built.error().format().c_str());
                    return nullptr;
                }
                return std::move(*built);
            });
        }
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Modal: {
        auto view = std::make_unique<ModalView>(node.id);
        apply_common_view_properties(*view, node);
        view->set_padding(node.layout.padding);
        view->set_backdrop(node.modal_backdrop);
        view->set_dismiss_on_backdrop(node.dismiss_on_backdrop);
        if (!node.action_id.empty() && context.dispatch_action) {
            const std::string action_id = node.action_id;
            const UiWidgetActionDispatcher dispatcher = context.dispatch_action;
            view->set_on_dismiss([dispatcher, action_id] { dispatcher(action_id); });
        }
        if (auto result = add_children(*view); !result) return result.error();
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Tooltip: {
        auto view = std::make_unique<TooltipView>(node.id);
        apply_common_view_properties(*view, node);
        view->set_text(node.text);
        view->set_text_style(node.text_style);
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Image: {
        auto view = std::make_unique<ImageView>(node.id);
        apply_common_view_properties(*view, node);
        view->set_image_key(node.image_key);
        view->set_tint(node.image_tint);
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::NineSlice: {
        auto view = std::make_unique<NineSliceView>(node.id);
        apply_common_view_properties(*view, node);
        view->set_image_key(node.image_key);
        view->set_borders(node.nine_slice_borders);
        view->set_tint(node.image_tint);
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Slot: {
        auto view = std::make_unique<SlotView>(node.id);
        apply_common_view_properties(*view, node);
        view->set_slot_state({.item_key = node.slot.item_key,
                              .count = node.slot.count,
                              .selected = node.slot.selected});
        return std::unique_ptr<View>(std::move(view));
    }
    case UiWidgetType::Scroll: {
        auto view = std::make_unique<ScrollView>(node.id);
        apply_common_view_properties(*view, node);
        view->set_scroll_axis(node.layout.scroll_axis);
        view->set_scroll_step(node.layout.scroll_step);
        if (!node.children.empty()) {
            auto content = instantiate_node(node.children.front(), context);
            if (!content) return content.error();
            view->set_content(std::move(*content));
        }
        return std::unique_ptr<View>(std::move(view));
    }
    }

    return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                            "UiPackedScene contains an unsupported widget type"};
}

}  // namespace

snt::core::Expected<void> validate_ui_widget_tree(const UiWidgetTree& tree) {
    size_t node_count = 0;
    std::unordered_set<std::string> ids;
    return validate_node(tree.root, "root", 0, node_count, ids);
}

snt::core::Expected<void> validate_ui_packed_scene(const UiPackedScene& scene) {
    if (scene.format_version != UiPackedScene::kCurrentFormatVersion) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "UiPackedScene has an unsupported format version"};
    }
    return validate_ui_widget_tree(scene.tree);
}

snt::core::Expected<UiPackedScene> parse_ui_packed_scene_json(std::string_view source,
                                                               std::string_view source_identity) {
    const std::string label = source_identity.empty() ? "<memory>" : std::string(source_identity);
    try {
        const json document = json::parse(source.begin(), source.end());
        if (!document.is_object()) return invalid_argument("UiPackedScene '" + label + "' must be an object");
        const auto format = document.find("format");
        if (format == document.end() || !format->is_string() ||
            format->get<std::string>() != "snt.ui.packed_scene") {
            return invalid_argument("UiPackedScene '" + label + "' has an invalid format marker");
        }
        const auto version = document.find("version");
        if (version == document.end() || !version->is_number_unsigned()) {
            return invalid_argument("UiPackedScene '" + label + "' requires an unsigned version");
        }
        const uint64_t parsed_version = version->get<uint64_t>();
        if (parsed_version > std::numeric_limits<uint32_t>::max()) {
            return invalid_argument("UiPackedScene '" + label + "' version is outside uint32 range");
        }
        const auto root = document.find("root");
        if (root == document.end()) return invalid_argument("UiPackedScene '" + label + "' requires a root node");
        auto parsed_root = parse_node(*root, "root", 0);
        if (!parsed_root) return parsed_root.error();

        UiPackedScene scene{
            .format_version = static_cast<uint32_t>(parsed_version),
            .tree = {.root = std::move(*parsed_root)},
        };
        if (auto result = validate_ui_packed_scene(scene); !result) {
            auto error = result.error();
            error.with_context("UiPackedScene '" + label + "'");
            return error;
        }
        return scene;
    } catch (const std::exception& error) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "UiPackedScene JSON parse error in '" + label + "': " + error.what()};
    }
}

snt::core::Expected<UiPackedScene> load_ui_packed_scene_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return snt::core::Error{snt::core::ErrorCode::kFileOpenFailed,
                                "Could not open UiPackedScene file: " + path.string()};
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    return parse_ui_packed_scene_json(source, path.string());
}

snt::core::Expected<std::unique_ptr<View>> instantiate_ui_widget_tree(
    const UiWidgetTree& tree,
    UiWidgetBuildContext context) {
    if (auto result = validate_ui_widget_tree(tree); !result) return result.error();
    return instantiate_node(tree.root, context);
}

snt::core::Expected<std::unique_ptr<View>> instantiate_ui_packed_scene(
    const UiPackedScene& scene,
    UiWidgetBuildContext context) {
    if (auto result = validate_ui_packed_scene(scene); !result) return result.error();
    return instantiate_ui_widget_tree(scene.tree, std::move(context));
}

UiScreenFactory make_ui_widget_tree_factory(std::shared_ptr<const UiWidgetTree> tree) {
    return [tree = std::move(tree)](const UiScreenMountContext& context)
        -> snt::core::Expected<UiScreenMount> {
        if (!tree) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "UiWidgetTree factory received a null tree"};
        }
        auto root = instantiate_ui_widget_tree(*tree, {
            .dispatch_action = context.dispatch_action,
        });
        if (!root) return root.error();
        return UiScreenMount{.root = std::move(*root)};
    };
}

UiScreenFactory make_ui_packed_scene_factory(std::shared_ptr<const UiPackedScene> scene) {
    return [scene = std::move(scene)](const UiScreenMountContext& context)
        -> snt::core::Expected<UiScreenMount> {
        if (!scene) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "UiPackedScene factory received a null scene"};
        }
        auto root = instantiate_ui_packed_scene(*scene, {
            .dispatch_action = context.dispatch_action,
        });
        if (!root) return root.error();
        return UiScreenMount{.root = std::move(*root)};
    };
}

UiWidgetTreeBuilder::UiWidgetTreeBuilder(UiWidgetType root_type, std::string root_id) {
    tree_.root.type = root_type;
    tree_.root.id = std::move(root_id);
}

UiWidgetTemplate& UiWidgetTreeBuilder::add_child(UiWidgetTemplate& parent,
                                                  UiWidgetTemplate child) {
    parent.children.push_back(std::move(child));
    return parent.children.back();
}

}  // namespace snt::ui

#include "core/path_utils.h"
#include "ui/mod_ui_internal.h"
#include "ui/retained_mui_runtime.h"
#include "ui/ui_packed_scene.h"
#include "ui/ui_packed_scene_catalog.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace snt::ui;

snt::core::Expected<snt::core::RuntimePathResolver> make_test_path_resolver() {
    return snt::core::RuntimePathResolver::create({
        .engine_root = SNT_ENGINE_TEST_ROOT,
        .game_root = SNT_ENGINE_TEST_ROOT,
        .user_root = SNT_ENGINE_TEST_ROOT,
    });
}

class RecordingUiTextInputPlatform final : public IUiTextInputPlatform {
public:
    snt::core::Expected<void> set_text_input_active(bool active) override {
        activations.push_back(active);
        return {};
    }

    snt::core::Expected<void> set_text_input_area(UiTextInputArea area) override {
        areas.push_back(area);
        return {};
    }

    std::vector<bool> activations;
    std::vector<UiTextInputArea> areas;
};

}  // namespace

TEST(RetainedMui, TextEngineShapesChineseAndEmoji) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UnicodeTextEngine engine(*paths);
    ASSERT_TRUE(engine.available()) << engine.initialization_error();
    EXPECT_TRUE(engine.capabilities().harfbuzz);
    EXPECT_TRUE(engine.capabilities().icu);
    EXPECT_TRUE(engine.capabilities().bidi);
    EXPECT_TRUE(engine.capabilities().color_emoji);
    TextStyle style;
    style.size_px = 18.0f;

    TextLayout layout = engine.shape("背包 Inventory 🎒", style);

    EXPECT_TRUE(layout.contains_cjk);
    EXPECT_TRUE(layout.contains_emoji);
    EXPECT_GT(layout.size.x, 0.0f);
    EXPECT_GT(layout.clusters.size(), 3u);
}

TEST(RetainedMui, UnicodeGlyphAtlasEmitsCjkAndColorEmojiQuads) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UnicodeTextEngine engine(*paths);
    ASSERT_TRUE(engine.available()) << engine.initialization_error();

    TextStyle style;
    style.size_px = 24.0f;
    const std::string text = "背包 🎒👩‍🚀";
    TextLayout layout = engine.shape(text, style);
    ASSERT_TRUE(layout.glyph_atlas);
    ASSERT_GT(layout.glyph_atlas->revision, 0u);
    ASSERT_FALSE(layout.glyphs.empty());

    Arc2DCommandBuffer commands;
    commands.text({.pos = {16.0f, 24.0f}, .size = {600.0f, 64.0f}}, text, style, layout);
    UiImageRegistry images;
    Arc2DRenderer renderer(images);
    UiDrawData draw_data = renderer.build_draw_data(commands);

    ASSERT_EQ(draw_data.glyph_atlas.get(), layout.glyph_atlas.get());
    ASSERT_FALSE(draw_data.vertices.empty());
    ASSERT_FALSE(draw_data.indices.empty());

    bool has_sdf = false;
    bool has_color = false;
    for (const UiVertex& vertex : draw_data.vertices) {
        has_sdf = has_sdf || vertex.texture_mode == UiTextureMode::SignedDistanceGlyph;
        has_color = has_color || vertex.texture_mode == UiTextureMode::ColorGlyph;
        if (vertex.texture_mode != UiTextureMode::Solid) {
            EXPECT_GE(vertex.uv[0], 0.0f);
            EXPECT_GE(vertex.uv[1], 0.0f);
            EXPECT_LE(vertex.uv[0], 1.0f);
            EXPECT_LE(vertex.uv[1], 1.0f);
        }
    }
    EXPECT_TRUE(has_sdf);
    EXPECT_TRUE(has_color);
}

TEST(RetainedMui, RoundedRectProducesRoundedSolidGeometry) {
    Arc2DCommandBuffer commands;
    commands.rect({.pos = {10.0f, 20.0f}, .size = {80.0f, 40.0f}},
                  {32, 64, 96, 255}, 8.0f);

    UiImageRegistry images;
    Arc2DRenderer renderer(images);
    const UiDrawData draw_data = renderer.build_draw_data(commands);
    EXPECT_EQ(draw_data.vertices.size(), 17u);
    EXPECT_EQ(draw_data.indices.size(), 48u);
    for (const UiVertex& vertex : draw_data.vertices) {
        EXPECT_EQ(vertex.texture_mode, UiTextureMode::Solid);
    }
}

TEST(RetainedMui, ImageAndClipCommandsCreateOrderedDrawBatches) {
    UiImageRegistry images;
    std::vector<uint8_t> pixels(2u * 2u * 4u, 255u);
    ASSERT_TRUE(images.register_rgba("test.icon", 2, 2, std::move(pixels)));

    Arc2DCommandBuffer commands;
    commands.rect({.pos = {0.0f, 0.0f}, .size = {20.0f, 20.0f}},
                  {30, 40, 50, 255});
    commands.push_clip({.pos = {5.0f, 5.0f}, .size = {12.0f, 10.0f}});
    commands.image({.pos = {2.0f, 2.0f}, .size = {16.0f, 16.0f}}, "test.icon");
    commands.pop_clip();
    commands.rect({.pos = {24.0f, 0.0f}, .size = {20.0f, 20.0f}},
                  {70, 80, 90, 255});

    Arc2DRenderer renderer(images);
    const UiDrawData draw_data = renderer.build_draw_data(commands);
    ASSERT_TRUE(draw_data.image_atlas);
    ASSERT_EQ(draw_data.indices.size(), 18u);
    ASSERT_EQ(draw_data.batches.size(), 3u);
    EXPECT_EQ(draw_data.batches[0].texture, UiTextureBinding::GlyphAtlas);
    EXPECT_EQ(draw_data.batches[1].texture, UiTextureBinding::ImageAtlas);
    EXPECT_TRUE(draw_data.batches[1].clip.enabled);
    EXPECT_FLOAT_EQ(draw_data.batches[1].clip.rect.pos.x, 5.0f);
    EXPECT_FLOAT_EQ(draw_data.batches[1].clip.rect.size.y, 10.0f);
    EXPECT_EQ(draw_data.batches[2].texture, UiTextureBinding::GlyphAtlas);

    const bool has_image_vertex = std::any_of(
        draw_data.vertices.begin(), draw_data.vertices.end(), [](const UiVertex& vertex) {
            return vertex.texture_mode == UiTextureMode::Image;
        });
    EXPECT_TRUE(has_image_vertex);
}

TEST(RetainedMui, DrawDataUsesThirtyTwoBitIndicesForDensePanels) {
    UiImageRegistry images;
    Arc2DCommandBuffer commands;
    constexpr int kRectCount = 17000;
    for (int index = 0; index < kRectCount; ++index) {
        commands.rect({.pos = {static_cast<float>(index), 0.0f}, .size = {1.0f, 1.0f}},
                      {255, 255, 255, 255});
    }

    Arc2DRenderer renderer(images);
    const UiDrawData draw_data = renderer.build_draw_data(commands);
    ASSERT_GT(draw_data.vertices.size(), 0xFFFFu);
    ASSERT_EQ(draw_data.batches.size(), 1u);
    ASSERT_FALSE(draw_data.indices.empty());
    EXPECT_EQ(draw_data.indices.back(), draw_data.vertices.size() - 1u);
}

TEST(RetainedMui, GridLayoutPlacesVisibleChildrenInRowsAndColumns) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    auto root = std::make_unique<FrameLayout>("root");
    auto grid = std::make_unique<GridLayout>("grid");
    grid->set_columns(3);
    grid->set_column_spacing(2.0f);
    grid->set_row_spacing(2.0f);
    grid->set_padding({3.0f, 4.0f, 3.0f, 4.0f});

    std::array<View*, 4> children{};
    for (size_t index = 0; index < children.size(); ++index) {
        auto child = std::make_unique<View>("cell_" + std::to_string(index));
        LayoutParams params;
        params.width = 10.0f;
        params.height = 20.0f;
        child->set_layout_params(params);
        children[index] = child.get();
        grid->add_child(std::move(child));
    }
    root->add_child(std::move(grid));
    runtime.layout(*root, {160.0f, 100.0f});

    EXPECT_EQ(children[0]->bounds().pos.x, 3.0f);
    EXPECT_EQ(children[0]->bounds().pos.y, 4.0f);
    EXPECT_EQ(children[1]->bounds().pos.x, 15.0f);
    EXPECT_EQ(children[2]->bounds().pos.x, 27.0f);
    EXPECT_EQ(children[3]->bounds().pos.x, 3.0f);
    EXPECT_EQ(children[3]->bounds().pos.y, 26.0f);
}

TEST(RetainedMui, ScrollViewScrollsWheelInputAndEmitsViewportClip) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    auto root = std::make_unique<FrameLayout>("root");
    auto scroll = std::make_unique<ScrollView>("scroll");
    ScrollView* raw_scroll = scroll.get();
    LayoutParams scroll_params;
    scroll_params.width = 100.0f;
    scroll_params.height = 40.0f;
    scroll->set_layout_params(scroll_params);

    auto content = std::make_unique<FlexLayout>("content");
    content->set_orientation(Orientation::Vertical);
    std::array<View*, 3> rows{};
    for (size_t index = 0; index < rows.size(); ++index) {
        auto row = std::make_unique<View>("row_" + std::to_string(index));
        LayoutParams row_params;
        row_params.width = 100.0f;
        row_params.height = 30.0f;
        row->set_layout_params(row_params);
        row->set_background({static_cast<uint8_t>(30 + index), 40, 50, 255});
        rows[index] = row.get();
        content->add_child(std::move(row));
    }
    scroll->set_content(std::move(content));
    root->add_child(std::move(scroll));

    runtime.layout(*root, {160.0f, 120.0f});
    EXPECT_FLOAT_EQ(raw_scroll->max_scroll_offset().y, 50.0f);
    const float before_second_row = rows[1]->bounds().pos.y;

    runtime.begin_input_frame({
        .pointer_position = {10.0f, 10.0f},
        .scroll_delta = {0.0f, -1.0f},
    });
    EXPECT_TRUE(runtime.dispatch_pointer_input(*root));
    EXPECT_FLOAT_EQ(raw_scroll->scroll_offset().y, 36.0f);
    EXPECT_LT(rows[1]->bounds().pos.y, before_second_row);

    const UiFrameResult frame = runtime.paint(*root);
    const bool has_push_clip = std::any_of(
        frame.commands.commands().begin(), frame.commands.commands().end(),
        [](const ArcDrawCommand& command) {
            return std::holds_alternative<PushClipCommand>(command);
        });
    const bool has_pop_clip = std::any_of(
        frame.commands.commands().begin(), frame.commands.commands().end(),
        [](const ArcDrawCommand& command) {
            return std::holds_alternative<PopClipCommand>(command);
        });
    EXPECT_TRUE(has_push_clip);
    EXPECT_TRUE(has_pop_clip);
}

TEST(RetainedMui, NestedScrollViewsKeepWheelInputAtNearestMovableViewport) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    auto root = std::make_unique<FrameLayout>("root");
    auto outer = std::make_unique<ScrollView>("outer_scroll");
    ScrollView* raw_outer = outer.get();
    outer->set_layout_params({.width = 120.0f, .height = 80.0f});

    auto outer_content = std::make_unique<FlexLayout>("outer_content");
    outer_content->set_orientation(Orientation::Vertical);

    auto inner = std::make_unique<ScrollView>("inner_scroll");
    ScrollView* raw_inner = inner.get();
    inner->set_layout_params({.width = 120.0f, .height = 40.0f});
    auto inner_content = std::make_unique<FlexLayout>("inner_content");
    inner_content->set_orientation(Orientation::Vertical);
    for (int index = 0; index < 3; ++index) {
        auto row = std::make_unique<View>("inner_row_" + std::to_string(index));
        row->set_layout_params({.width = 120.0f, .height = 30.0f});
        inner_content->add_child(std::move(row));
    }
    inner->set_content(std::move(inner_content));
    outer_content->add_child(std::move(inner));

    auto spacer = std::make_unique<View>("outer_spacer");
    spacer->set_layout_params({.width = 120.0f, .height = 100.0f});
    outer_content->add_child(std::move(spacer));
    outer->set_content(std::move(outer_content));
    root->add_child(std::move(outer));

    runtime.layout(*root, {160.0f, 120.0f});
    ASSERT_FLOAT_EQ(raw_inner->max_scroll_offset().y, 50.0f);
    ASSERT_GT(raw_outer->max_scroll_offset().y, 0.0f);

    const UiInputState wheel = {
        .pointer_position = {10.0f, 10.0f},
        .scroll_delta = {0.0f, -1.0f},
    };
    runtime.begin_input_frame(wheel);
    EXPECT_TRUE(runtime.dispatch_pointer_input(*root));
    EXPECT_FLOAT_EQ(raw_inner->scroll_offset().y, 36.0f);
    EXPECT_FLOAT_EQ(raw_outer->scroll_offset().y, 0.0f);

    runtime.begin_input_frame(wheel);
    EXPECT_TRUE(runtime.dispatch_pointer_input(*root));
    EXPECT_FLOAT_EQ(raw_inner->scroll_offset().y, 50.0f);
    EXPECT_FLOAT_EQ(raw_outer->scroll_offset().y, 0.0f);

    runtime.begin_input_frame(wheel);
    EXPECT_TRUE(runtime.dispatch_pointer_input(*root));
    EXPECT_FLOAT_EQ(raw_inner->scroll_offset().y, 50.0f);
    EXPECT_FLOAT_EQ(raw_outer->scroll_offset().y, 36.0f);
}

TEST(RetainedMui, LayerStackNamespacedLifecycleMountsOnceAndUpdatesVisibleRoots) {
    UiImageRegistry images;
    UiLayerStack layers;
    int mount_count = 0;
    int update_count = 0;
    std::vector<std::string> invalidated_roots;
    layers.set_retained_root_invalidator([&invalidated_roots](View& root) {
        EXPECT_EQ(root.kind(), ViewKind::FrameLayout);
        invalidated_roots.push_back(root.id());
    });

    ASSERT_TRUE(layers.register_screen({
        .owner_id = "example_expansion",
        .screen_id = "research",
        .layer = UiLayer::Modal,
        .initially_visible = false,
        .factory = [&mount_count, &update_count](const UiScreenMountContext& context)
            -> snt::core::Expected<UiScreenMount> {
            ++mount_count;
            EXPECT_FLOAT_EQ(context.viewport.x, 1280.0f);
            EXPECT_FLOAT_EQ(context.viewport.y, 720.0f);
            auto root = std::make_unique<FrameLayout>("caller_supplied_root_id");
            root->set_background({20, 30, 40, 255});
            return UiScreenMount{
                .root = std::move(root),
                .update = [&update_count](View&, const UiScreenFrameContext& update_context) {
                    ++update_count;
                    EXPECT_FLOAT_EQ(update_context.viewport.x, 1280.0f);
                    EXPECT_FLOAT_EQ(update_context.viewport.y, 720.0f);
                },
            };
        },
    }));
    EXPECT_TRUE(layers.is_registered("example_expansion", "research"));
    EXPECT_FALSE(layers.is_visible("example_expansion", "research"));
    EXPECT_TRUE(layers.prepare_frame({.viewport = {1280.0f, 720.0f}, .images = images}).empty());

    EXPECT_FALSE(layers.register_screen({
        .owner_id = "example_expansion",
        .screen_id = "research",
        .factory = [](const UiScreenMountContext&)
            -> snt::core::Expected<UiScreenMount> { return UiScreenMount{}; },
    }));
    ASSERT_TRUE(layers.set_visible("example_expansion", "research", true));

    const auto& visible = layers.prepare_frame({.viewport = {1280.0f, 720.0f}, .images = images});
    ASSERT_EQ(visible.size(), 1u);
    ASSERT_NE(visible.front().root, nullptr);
    EXPECT_EQ(visible.front().layer, UiLayer::Modal);
    EXPECT_EQ(visible.front().root->id(), "example_expansion:research");
    EXPECT_EQ(mount_count, 1);
    EXPECT_EQ(update_count, 1);
    View* const mounted_root = visible.front().root;

    const auto& next_frame = layers.prepare_frame({.viewport = {1280.0f, 720.0f}, .images = images});
    ASSERT_EQ(next_frame.size(), 1u);
    EXPECT_EQ(next_frame.front().root, mounted_root);
    EXPECT_EQ(mount_count, 1);
    EXPECT_EQ(update_count, 2);

    EXPECT_EQ(layers.unregister_owner("example_expansion"), 1u);
    EXPECT_FALSE(layers.is_registered("example_expansion", "research"));
    EXPECT_EQ(invalidated_roots,
              std::vector<std::string>{"example_expansion:research"});
    EXPECT_TRUE(layers.prepare_frame({.viewport = {1280.0f, 720.0f}, .images = images}).empty());
}

TEST(RetainedMui, PackedSceneJsonInstantiatesWidgetsAndDispatchesActions) {
    constexpr std::string_view source = R"json(
{
  "format": "snt.ui.packed_scene",
  "version": 3,
  "root": {
    "type": "frame",
    "id": "research_root",
    "layout": { "width": 0, "height": 0, "padding": [8, 8, 8, 8] },
    "background": { "color": [14, 20, 30, 240], "radius": 6 },
    "children": [
      {
        "type": "flex",
        "id": "research_panel",
        "layout": { "orientation": "vertical", "spacing": 6 },
        "children": [
          {
            "type": "text",
            "id": "research_title",
            "text": "Research",
            "text_style": { "size": 18, "color": [230, 236, 245, 255] }
          },
          {
            "type": "button",
            "id": "claim_reward",
            "text": "Claim",
            "action": "research.claim"
          }
        ]
      }
    ]
  }
}
)json";

    auto scene = parse_ui_packed_scene_json(source, "research.mui.json");
    ASSERT_TRUE(scene) << scene.error().format();

    std::string dispatched_action;
    auto root = instantiate_ui_packed_scene(*scene, {
        .dispatch_action = [&dispatched_action](std::string_view action_id) {
            dispatched_action = action_id;
        },
    });
    ASSERT_TRUE(root) << root.error().format();
    EXPECT_EQ((*root)->kind(), ViewKind::FrameLayout);
    auto* root_group = dynamic_cast<ViewGroup*>(root->get());
    ASSERT_NE(root_group, nullptr);

    auto* title = dynamic_cast<TextView*>(root_group->find("research_title"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), "Research");

    auto* button = dynamic_cast<Button*>(root_group->find("claim_reward"));
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->activate());
    EXPECT_EQ(dispatched_action, "research.claim");
}

TEST(RetainedMui, PackedSceneV3InstantiatesInteractiveControlsAndVirtualList) {
    constexpr std::string_view source = R"json(
{
  "format": "snt.ui.packed_scene",
  "version": 3,
  "root": {
    "type": "frame",
    "id": "settings_root",
    "layout": { "width": 0, "height": 0 },
    "children": [
      {
        "type": "text_input",
        "id": "display_name",
        "placeholder": "Display name",
        "max_text_bytes": 32,
        "action": "profile.submit"
      },
      {
        "type": "text_editor",
        "id": "chat_draft",
        "text": "first\nsecond",
        "min_text_lines": 4,
        "action": "chat.send"
      },
      {
        "type": "checkbox",
        "id": "fullscreen",
        "text": "Fullscreen",
        "checked": true,
        "action": "video.fullscreen"
      },
      {
        "type": "slider",
        "id": "volume",
        "minimum": 0,
        "maximum": 1,
        "step": 0.25,
        "value": 0.5,
        "action": "audio.volume"
      },
      {
        "type": "virtual_list",
        "id": "recent_servers",
        "layout": { "width": 160, "height": 40 },
        "virtual_item_count": 100,
        "virtual_item_extent": 20,
        "children": [
          { "type": "text", "id": "server_row", "text": "Server" }
        ]
      },
      {
        "type": "modal",
        "id": "confirm_dialog",
        "dismiss_on_backdrop": true,
        "action": "dialog.dismiss",
        "children": [
          { "type": "text", "id": "dialog_message", "text": "Apply settings?" }
        ]
      },
      { "type": "tooltip", "id": "volume_tip", "text": "Master volume" }
    ]
  }
}
)json";

    auto scene = parse_ui_packed_scene_json(source, "settings.mui.json");
    ASSERT_TRUE(scene) << scene.error().format();

    std::vector<std::string> actions;
    auto root = instantiate_ui_packed_scene(*scene, {
        .dispatch_action = [&actions](std::string_view action_id) {
            actions.emplace_back(action_id);
        },
    });
    ASSERT_TRUE(root) << root.error().format();
    auto* group = dynamic_cast<ViewGroup*>(root->get());
    ASSERT_NE(group, nullptr);

    auto* input = dynamic_cast<TextInput*>(group->find("display_name"));
    auto* editor = dynamic_cast<TextEditor*>(group->find("chat_draft"));
    auto* checkbox = dynamic_cast<Checkbox*>(group->find("fullscreen"));
    auto* slider = dynamic_cast<Slider*>(group->find("volume"));
    auto* list = dynamic_cast<VirtualListView*>(group->find("recent_servers"));
    auto* modal = dynamic_cast<ModalView*>(group->find("confirm_dialog"));
    EXPECT_NE(dynamic_cast<TooltipView*>(group->find("volume_tip")), nullptr);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(editor, nullptr);
    ASSERT_NE(checkbox, nullptr);
    ASSERT_NE(slider, nullptr);
    ASSERT_NE(list, nullptr);
    ASSERT_NE(modal, nullptr);
    EXPECT_EQ(input->placeholder(), "Display name");
    EXPECT_EQ(editor->text(), "first\nsecond");
    EXPECT_EQ(editor->min_visible_lines(), 4u);
    EXPECT_TRUE(checkbox->checked());
    EXPECT_FLOAT_EQ(slider->value(), 0.5f);
    EXPECT_EQ(list->item_count(), 100u);

    input->on_input_event({.type = UiInputEventType::TextCommit, .text = "Ada"});
    input->on_input_event({.type = UiInputEventType::KeyDown, .key = UiKey::Enter});
    editor->on_input_event({.type = UiInputEventType::KeyDown, .key = UiKey::Enter});
    EXPECT_EQ(editor->text(), "first\nsecond\n");
    editor->on_input_event({.type = UiInputEventType::KeyDown,
                            .key = UiKey::Enter,
                            .modifiers = UiKeyModifier::Control});
    checkbox->on_input_event({.type = UiInputEventType::PointerUp,
                              .pointer_button = UiPointerButton::Primary,
                              .activation = true});
    slider->set_value(0.75f, true);
    modal->on_input_event({.type = UiInputEventType::KeyDown, .key = UiKey::Escape});
    EXPECT_EQ(actions, (std::vector<std::string>{
        "profile.submit", "chat.send", "video.fullscreen", "audio.volume", "dialog.dismiss",
    }));

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    runtime.layout(**root, {320.0f, 200.0f});
    EXPECT_LT(list->children().size(), list->item_count());
}

TEST(RetainedMui, DynamicWidgetBuilderAndLayerStackShareOneTreePath) {
    UiWidgetTreeBuilder builder(UiWidgetType::Flex, "dynamic_root");
    builder.root().layout.orientation = Orientation::Vertical;
    builder.root().layout.spacing = 4.0f;
    builder.root().background = Color{20, 28, 40, 255};
    builder.root().background_radius = 4.0f;

    UiWidgetTemplate label;
    label.type = UiWidgetType::Text;
    label.id = "dynamic_label";
    label.text = "Dynamic widget tree";
    label.text_style.size_px = 15.0f;
    builder.add_child(builder.root(), std::move(label));

    UiWidgetTemplate button;
    button.type = UiWidgetType::Button;
    button.id = "dynamic_button";
    button.text = "Run";
    button.action_id = "dynamic.run";
    builder.add_child(builder.root(), std::move(button));

    auto tree = std::make_shared<const UiWidgetTree>(std::move(builder).finish());
    ASSERT_TRUE(validate_ui_widget_tree(*tree));

    UiImageRegistry images;
    UiLayerStack layers;
    std::string dispatched_action;
    ASSERT_TRUE(layers.register_screen({
        .owner_id = "example_expansion",
        .screen_id = "dynamic",
        .layer = UiLayer::Modal,
        .initially_visible = true,
        .factory = make_ui_widget_tree_factory(tree),
        .dispatch_action = [&dispatched_action](std::string_view action_id) {
            dispatched_action = action_id;
        },
    }));

    const auto& visible = layers.prepare_frame({.viewport = {800.0f, 600.0f}, .images = images});
    ASSERT_EQ(visible.size(), 1u);
    ASSERT_NE(visible.front().root, nullptr);
    EXPECT_EQ(visible.front().root->id(), "example_expansion:dynamic");
    EXPECT_TRUE(layers.is_mounted("example_expansion", "dynamic"));
    auto* root_group = dynamic_cast<ViewGroup*>(visible.front().root);
    ASSERT_NE(root_group, nullptr);
    EXPECT_NE(root_group->find("dynamic_label"), nullptr);

    auto* button_view = dynamic_cast<Button*>(root_group->find("dynamic_button"));
    ASSERT_NE(button_view, nullptr);
    EXPECT_TRUE(button_view->activate());
    EXPECT_EQ(dispatched_action, "dynamic.run");
}

TEST(RetainedMui, PackedSceneFileOwnerLifecycleRetainsMountedTreeOnFailedReload) {
    const std::filesystem::path resource_path = std::filesystem::path(SNT_ENGINE_TEST_ROOT) /
        "tests" / "assets" / "ui" / "research_panel.mui.json";

    UiImageRegistry images;
    UiLayerStack layers;
    std::string dispatched_action;
    ASSERT_TRUE(load_ui_packed_scene_owner(
        layers,
        "example_expansion",
        {{
            .screen_id = "research",
            .source_path = resource_path,
            .layer = UiLayer::Modal,
            .initially_visible = true,
            .dispatch_action = [&dispatched_action](std::string_view action_id) {
                dispatched_action = action_id;
            },
        }}));

    const auto& visible = layers.prepare_frame({.viewport = {800.0f, 600.0f}, .images = images});
    ASSERT_EQ(visible.size(), 1u);
    ASSERT_NE(visible.front().root, nullptr);
    View* const mounted_root = visible.front().root;
    EXPECT_EQ(mounted_root->id(), "example_expansion:research");

    auto* root_group = dynamic_cast<ViewGroup*>(mounted_root);
    ASSERT_NE(root_group, nullptr);
    auto* title = dynamic_cast<TextView*>(root_group->find("research_title"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), "Research Archive");
    auto* button = dynamic_cast<Button*>(root_group->find("research_claim"));
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->activate());
    EXPECT_EQ(dispatched_action, "research.claim");

    const auto& same_tree = layers.prepare_frame({.viewport = {800.0f, 600.0f}, .images = images});
    ASSERT_EQ(same_tree.size(), 1u);
    EXPECT_EQ(same_tree.front().root, mounted_root);

    UiPackedScene invalid_scene;
    invalid_scene.tree.root.type = UiWidgetType::Frame;
    invalid_scene.tree.root.id = "duplicate";
    UiWidgetTemplate duplicate_child;
    duplicate_child.type = UiWidgetType::Text;
    duplicate_child.id = "duplicate";
    invalid_scene.tree.root.children.push_back(std::move(duplicate_child));
    EXPECT_FALSE(replace_ui_packed_scene_owner(
        layers,
        "example_expansion",
        {{
            .screen_id = "research",
            .scene = std::make_shared<const UiPackedScene>(std::move(invalid_scene)),
            .layer = UiLayer::Modal,
            .initially_visible = true,
        }}));

    const auto& after_failed_reload = layers.prepare_frame(
        {.viewport = {800.0f, 600.0f}, .images = images});
    ASSERT_EQ(after_failed_reload.size(), 1u);
    EXPECT_EQ(after_failed_reload.front().root, mounted_root);
    EXPECT_TRUE(layers.is_registered("example_expansion", "research"));
    EXPECT_EQ(layers.unregister_owner("example_expansion"), 1u);
    EXPECT_TRUE(layers.prepare_frame({.viewport = {800.0f, 600.0f}, .images = images}).empty());
}

TEST(RetainedMui, PackedSceneRejectsDuplicateIdsAndInvalidScrollContent) {
    constexpr std::string_view duplicate_ids = R"json(
{
  "format": "snt.ui.packed_scene",
  "version": 3,
  "root": {
    "type": "frame",
    "id": "root",
    "children": [{ "type": "text", "id": "root", "text": "duplicate" }]
  }
}
)json";
    EXPECT_FALSE(parse_ui_packed_scene_json(duplicate_ids, "duplicate.mui.json"));

    constexpr std::string_view invalid_scroll = R"json(
{
  "format": "snt.ui.packed_scene",
  "version": 3,
  "root": {
    "type": "scroll",
    "id": "scroll",
    "children": [
      { "type": "text", "id": "first", "text": "one" },
      { "type": "text", "id": "second", "text": "two" }
    ]
  }
}
)json";
    EXPECT_FALSE(parse_ui_packed_scene_json(invalid_scroll, "invalid_scroll.mui.json"));
}

TEST(RetainedMui, MixedBidiAndJoinedEmojiProduceGlyphs) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UnicodeTextEngine engine(*paths);
    ASSERT_TRUE(engine.available()) << engine.initialization_error();

    TextStyle style;
    style.size_px = 20.0f;
    TextLayout layout = engine.shape("玩家 abc العربية 👩‍🚀", style);

    EXPECT_TRUE(layout.contains_cjk);
    EXPECT_TRUE(layout.contains_emoji);
    EXPECT_FALSE(layout.glyphs.empty());
    EXPECT_GT(layout.size.x, 0.0f);

    TextLayout visual_order = engine.shape("abc אבג", style);
    std::vector<uint32_t> hebrew_clusters;
    for (const TextCluster& cluster : visual_order.clusters) {
        if (cluster.first_codepoint >= 0x05D0u && cluster.first_codepoint <= 0x05EAu) {
            hebrew_clusters.push_back(cluster.first_codepoint);
        }
    }
    ASSERT_EQ(hebrew_clusters.size(), 3u);
    EXPECT_EQ(hebrew_clusters[0], 0x05D2u);
    EXPECT_EQ(hebrew_clusters[1], 0x05D1u);
    EXPECT_EQ(hebrew_clusters[2], 0x05D0u);
}

TEST(RetainedMui, ViewModelBindingUpdatesTextView) {
    ViewModel model;
    auto text = std::make_unique<TextView>("title");
    TextView* raw = text.get();
    raw->bind_text(model, "screen.title");

    model.set("screen.title", std::string("合成 Crafting ⚒"));

    EXPECT_EQ(raw->text(), "合成 Crafting ⚒");
}

TEST(RetainedMui, ViewModelSubscriptionDisconnectsWithoutDanglingCallbacks) {
    ViewModel model;
    int notifications = 0;
    auto subscription = model.bind("screen.title", [&](std::string_view,
                                                        const BindingValue&) {
        ++notifications;
    });
    ASSERT_TRUE(subscription.connected());

    model.set("screen.title", std::string("first"));
    EXPECT_EQ(notifications, 1);

    subscription.reset();
    EXPECT_FALSE(subscription.connected());
    model.set("screen.title", std::string("second"));
    EXPECT_EQ(notifications, 1);

    {
        auto text = std::make_unique<TextView>("title");
        text->bind_text(model, "screen.title");
    }
    model.set("screen.title", std::string("after-view-destruction"));
    EXPECT_EQ(notifications, 1);

    int nested_notifications = 0;
    auto secondary = model.bind("nested.secondary", [&](std::string_view,
                                                          const BindingValue&) {
        ++nested_notifications;
    });
    auto primary = model.bind("nested.primary", [&](std::string_view,
                                                      const BindingValue&) {
        model.set("nested.secondary", int64_t{7});
    });
    model.set("nested.primary", int64_t{3});
    EXPECT_EQ(nested_notifications, 1);

    model.set("reentrant.initial", int64_t{1});
    int initial_notifications = 0;
    ViewModel::Subscription nested_subscription;
    auto reentrant = model.bind("reentrant.initial", [&](std::string_view,
                                                            const BindingValue&) {
        ++initial_notifications;
        nested_subscription = model.bind("reentrant.initial", [](std::string_view,
                                                                     const BindingValue&) {});
    });
    EXPECT_EQ(initial_notifications, 1);
    EXPECT_TRUE(reentrant.connected());
    EXPECT_TRUE(nested_subscription.connected());
}

TEST(RetainedMui, DragSessionCancelsSourceAndHoveredTargetWhenInputIsInterrupted) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    auto root = std::make_unique<FrameLayout>("inventory_root");
    auto source = std::make_unique<SlotView>("source_slot");
    auto target = std::make_unique<SlotView>("target_slot");
    SlotView* const raw_source = source.get();
    SlotView* const raw_target = target.get();
    source->set_layout_params({.width = 36.0f, .height = 36.0f});
    target->set_layout_params({.width = 36.0f,
                               .height = 36.0f,
                               .margin = {.left = 64.0f}});
    source->set_slot_state({.item_key = "item.iron", .count = 4});

    std::vector<UiDragEvent> source_events;
    std::vector<UiDragEvent> target_events;
    source->set_drag_handler([&source_events](const UiDragEvent& event) {
        source_events.push_back(event);
    });
    target->set_drag_handler([&target_events](const UiDragEvent& event) {
        target_events.push_back(event);
    });
    root->add_child(std::move(source));
    root->add_child(std::move(target));
    runtime.layout(*root, {160.0f, 80.0f});

    std::array<View*, 1> active_roots{root.get()};
    runtime.begin_input_frame({
        .pointer_position = {12.0f, 12.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    }, active_roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));
    ASSERT_NE(runtime.drag_session(), nullptr);
    ASSERT_EQ(source_events.size(), 1u);
    EXPECT_EQ(source_events.front().type, UiDragEventType::Begin);

    runtime.begin_input_frame({
        .pointer_position = {76.0f, 12.0f},
        .pointer_held = {true, false, false},
    }, active_roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));
    ASSERT_EQ(target_events.size(), 1u);
    EXPECT_EQ(target_events.front().type, UiDragEventType::Enter);
    EXPECT_EQ(target_events.front().source_id, raw_source->id());

    runtime.begin_input_frame({.pointer_enabled = false}, active_roots);
    EXPECT_EQ(runtime.drag_session(), nullptr);
    ASSERT_EQ(source_events.size(), 2u);
    EXPECT_EQ(source_events.back().type, UiDragEventType::Cancel);
    EXPECT_EQ(source_events.back().target_id, raw_target->id());
    ASSERT_EQ(target_events.size(), 2u);
    EXPECT_EQ(target_events.back().type, UiDragEventType::Cancel);
    EXPECT_EQ(target_events.back().source_id, raw_source->id());
}

TEST(RetainedMui, SlotDragStartUsesSecondaryButtonForHalfStack) {
    SlotView slot("slot");
    slot.set_slot_state({.item_key = "item.iron", .count = 7});

    const auto primary = slot.begin_drag({.pointer_button = UiPointerButton::Primary});
    ASSERT_TRUE(primary.has_value());
    EXPECT_EQ(primary->count, 7);

    const auto secondary = slot.begin_drag({.pointer_button = UiPointerButton::Secondary});
    ASSERT_TRUE(secondary.has_value());
    EXPECT_EQ(secondary->count, 4);

    slot.set_enabled(false);
    EXPECT_FALSE(slot.begin_drag({.pointer_button = UiPointerButton::Primary}).has_value());
    EXPECT_FALSE(slot.accepts_drop(*primary));
}

TEST(RetainedMui, LayerStackInvalidationDeliversFocusLostBeforeRetainedRootIsHidden) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    int focus_lost = 0;

    ASSERT_TRUE(runtime.layers().register_screen({
        .owner_id = "builtin",
        .screen_id = "name_prompt",
        .initially_visible = true,
        .factory = [&focus_lost](const UiScreenMountContext&)
            -> snt::core::Expected<UiScreenMount> {
            auto root = std::make_unique<FrameLayout>("name_prompt_root");
            auto editor = std::make_unique<TextInput>("name_editor");
            editor->set_layout_params({.width = 180.0f, .height = 32.0f});
            editor->set_input_handler([&focus_lost](const UiInputEvent& event) {
                if (event.type == UiInputEventType::FocusLost) ++focus_lost;
                return UiEventReply::Ignored;
            });
            root->add_child(std::move(editor));
            return UiScreenMount{.root = std::move(root)};
        },
    }));

    const auto& submissions = runtime.layers().prepare_frame({
        .viewport = {240.0f, 80.0f},
        .images = runtime.images(),
    });
    ASSERT_EQ(submissions.size(), 1u);
    View* const root = submissions.front().root;
    ASSERT_NE(root, nullptr);
    runtime.layout(*root, {240.0f, 80.0f});
    std::array<View*, 1> active_roots{root};

    runtime.begin_input_frame({
        .pointer_position = {12.0f, 12.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    }, active_roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));
    EXPECT_TRUE(runtime.focused_text_input_bounds(*root).has_value());

    ASSERT_TRUE(runtime.layers().set_visible("builtin", "name_prompt", false));
    EXPECT_EQ(focus_lost, 1);
    EXPECT_FALSE(runtime.focused_text_input_bounds(*root).has_value());
}

TEST(RetainedMui, PointerEventUsesCaptureTargetAndBubblePhases) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    auto root = std::make_unique<FrameLayout>("root");
    auto parent = std::make_unique<FlexLayout>("parent");
    auto button = std::make_unique<Button>("target");
    button->set_layout_params({.width = 120.0f, .height = 40.0f});

    std::vector<std::string> order;
    const auto record = [&order](std::string name) {
        return [&order, name = std::move(name)](const UiInputEvent& event) {
            if (event.type == UiInputEventType::PointerDown) {
                switch (event.phase) {
                    case UiEventPhase::Capture: order.push_back(name + ":capture"); break;
                    case UiEventPhase::Target: order.push_back(name + ":target"); break;
                    case UiEventPhase::Bubble: order.push_back(name + ":bubble"); break;
                }
            }
            return UiEventReply::Ignored;
        };
    };
    root->set_input_handler(record("root"));
    parent->set_input_handler(record("parent"));
    button->set_input_handler(record("target"));
    parent->add_child(std::move(button));
    root->add_child(std::move(parent));
    runtime.layout(*root, {320.0f, 180.0f});

    runtime.begin_input_frame({
        .pointer_position = {20.0f, 20.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    });
    EXPECT_TRUE(runtime.dispatch_pointer_input(*root));
    EXPECT_EQ(order, (std::vector<std::string>{
        "root:capture", "parent:capture", "target:target", "parent:bubble", "root:bubble",
    }));
}

TEST(RetainedMui, ButtonRoutesPointerAndKeyboardActivation) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    auto root = std::make_unique<FrameLayout>("test_root");
    auto button = std::make_unique<Button>("confirm");
    Button* raw_button = button.get();
    LayoutParams params;
    params.width = 120.0f;
    params.height = 40.0f;
    params.margin = {20.0f, 20.0f, 0.0f, 0.0f};
    button->set_layout_params(params);
    button->set_text("Confirm");
    int activations = 0;
    button->set_on_activate([&] { ++activations; });
    root->add_child(std::move(button));
    runtime.layout(*root, {320.0f, 180.0f});

    runtime.begin_input_frame({
        .pointer_position = {40.0f, 40.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    });
    EXPECT_TRUE(runtime.dispatch_pointer_input(*root));
    runtime.synchronize_interaction_state(*root);
    EXPECT_TRUE(has_interaction_state(raw_button->interaction_state(),
                                      UiInteractionState::Pressed));
    EXPECT_TRUE(has_interaction_state(raw_button->interaction_state(),
                                      UiInteractionState::Focused));

    runtime.begin_input_frame({
        .pointer_position = {40.0f, 40.0f},
        .pointer_released = {true, false, false},
    });
    EXPECT_TRUE(runtime.dispatch_pointer_input(*root));
    runtime.synchronize_interaction_state(*root);
    EXPECT_EQ(activations, 1);
    EXPECT_FALSE(has_interaction_state(raw_button->interaction_state(),
                                       UiInteractionState::Pressed));

    runtime.begin_input_frame({.pressed_keys = {UiKey::Enter}});
    EXPECT_TRUE(runtime.dispatch_keyboard_input(*root));
    EXPECT_EQ(activations, 2);
}

TEST(RetainedMui, TabFocusTraversalSkipsHiddenAndDisabledControls) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    FrameLayout root("focus_root");
    std::vector<std::string> focus_events;
    const auto record_focus = [&focus_events](std::string id) {
        return [&focus_events, id = std::move(id)](const UiInputEvent& event) {
            if (event.type == UiInputEventType::FocusGained) {
                focus_events.push_back(id + ":gained");
            } else if (event.type == UiInputEventType::FocusLost) {
                focus_events.push_back(id + ":lost");
            }
            return UiEventReply::Ignored;
        };
    };

    auto first = std::make_unique<Button>("first");
    Button* const raw_first = first.get();
    first->set_input_handler(record_focus("first"));

    auto hidden = std::make_unique<Button>("hidden");
    hidden->set_visibility(Visibility::Hidden);
    hidden->set_input_handler(record_focus("hidden"));

    auto disabled = std::make_unique<Button>("disabled");
    disabled->set_enabled(false);
    disabled->set_input_handler(record_focus("disabled"));

    auto editor = std::make_unique<TextInput>("editor");
    TextInput* const raw_editor = editor.get();
    editor->set_input_handler(record_focus("editor"));

    auto last = std::make_unique<Checkbox>("last");
    Checkbox* const raw_last = last.get();
    last->set_input_handler(record_focus("last"));

    root.add_child(std::move(first));
    root.add_child(std::move(hidden));
    root.add_child(std::move(disabled));
    root.add_child(std::move(editor));
    root.add_child(std::move(last));
    std::array<View*, 1> active_roots{&root};

    runtime.begin_input_frame({.pressed_keys = {UiKey::Tab}}, active_roots);
    EXPECT_TRUE(runtime.dispatch_keyboard_input(root));
    runtime.synchronize_interaction_state(root);
    EXPECT_TRUE(has_interaction_state(raw_first->interaction_state(), UiInteractionState::Focused));

    runtime.begin_input_frame({.pressed_keys = {UiKey::Tab}}, active_roots);
    EXPECT_TRUE(runtime.dispatch_keyboard_input(root));
    runtime.synchronize_interaction_state(root);
    EXPECT_TRUE(has_interaction_state(raw_editor->interaction_state(), UiInteractionState::Focused));

    runtime.begin_input_frame({
        .modifiers = UiKeyModifier::Shift,
        .pressed_keys = {UiKey::Tab},
    }, active_roots);
    EXPECT_TRUE(runtime.dispatch_keyboard_input(root));
    runtime.synchronize_interaction_state(root);
    EXPECT_TRUE(has_interaction_state(raw_first->interaction_state(), UiInteractionState::Focused));

    runtime.begin_input_frame({
        .modifiers = UiKeyModifier::Shift,
        .pressed_keys = {UiKey::Tab},
    }, active_roots);
    EXPECT_TRUE(runtime.dispatch_keyboard_input(root));
    runtime.synchronize_interaction_state(root);
    EXPECT_TRUE(has_interaction_state(raw_last->interaction_state(), UiInteractionState::Focused));

    EXPECT_EQ(focus_events, (std::vector<std::string>{
        "first:gained",
        "first:lost", "editor:gained",
        "editor:lost", "first:gained",
        "first:lost", "last:gained",
    }));
}

TEST(RetainedMui, FocusedTextInputReleasesImeWhenDisabledOrHidden) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    auto platform = std::make_shared<RecordingUiTextInputPlatform>();
    runtime.set_text_input_platform(platform);

    auto root = std::make_unique<FrameLayout>("focus_lifecycle_root");
    auto container = std::make_unique<FrameLayout>("input_container");
    FrameLayout* const raw_container = container.get();
    container->set_layout_params({.width = 180.0f, .height = 32.0f});
    auto input = std::make_unique<TextInput>("editor");
    TextInput* const raw_input = input.get();
    input->set_layout_params({.width = 180.0f, .height = 32.0f});
    int focus_lost = 0;
    input->set_input_handler([&focus_lost](const UiInputEvent& event) {
        if (event.type == UiInputEventType::FocusLost) ++focus_lost;
        return UiEventReply::Ignored;
    });
    container->add_child(std::move(input));
    root->add_child(std::move(container));
    runtime.layout(*root, {240.0f, 80.0f});
    std::array<View*, 1> roots{root.get()};

    runtime.begin_input_frame({
        .pointer_position = {10.0f, 10.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));
    runtime.synchronize_text_input_platform(roots);
    ASSERT_EQ(platform->activations, (std::vector<bool>{true}));

    runtime.begin_input_frame({
        .pointer_position = {10.0f, 10.0f},
        .pointer_released = {true, false, false},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));

    raw_input->set_enabled(false);
    EXPECT_FALSE(runtime.focused_text_input_bounds(*root).has_value());
    runtime.begin_input_frame({.text_commits = {"ignored"}}, roots);
    EXPECT_FALSE(runtime.dispatch_keyboard_input(*root));
    EXPECT_TRUE(raw_input->text().empty());
    runtime.synchronize_interaction_state(*root);
    runtime.synchronize_text_input_platform(roots);
    EXPECT_EQ(focus_lost, 1);
    EXPECT_EQ(platform->activations, (std::vector<bool>{true, false}));

    raw_input->set_enabled(true);
    runtime.begin_input_frame({
        .pointer_position = {10.0f, 10.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));
    runtime.synchronize_text_input_platform(roots);
    ASSERT_EQ(platform->activations, (std::vector<bool>{true, false, true}));

    raw_container->set_visibility(Visibility::Hidden);
    EXPECT_FALSE(runtime.focused_text_input_bounds(*root).has_value());
    runtime.synchronize_interaction_state(*root);
    runtime.synchronize_text_input_platform(roots);
    EXPECT_EQ(focus_lost, 2);
    EXPECT_EQ(platform->activations, (std::vector<bool>{true, false, true, false}));

    runtime.synchronize_interaction_state(*root);
    runtime.synchronize_text_input_platform(roots);
    EXPECT_EQ(focus_lost, 2);
    EXPECT_EQ(platform->activations, (std::vector<bool>{true, false, true, false}));
}

TEST(RetainedMui, ClickingNonFocusableTargetClearsFocusedView) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    auto root = std::make_unique<FrameLayout>("click_focus_root");
    auto input = std::make_unique<TextInput>("editor");
    input->set_layout_params({.width = 120.0f, .height = 32.0f});
    int focus_lost = 0;
    input->set_input_handler([&focus_lost](const UiInputEvent& event) {
        if (event.type == UiInputEventType::FocusLost) ++focus_lost;
        return UiEventReply::Ignored;
    });
    root->add_child(std::move(input));

    auto non_focusable = std::make_unique<View>("background_target");
    non_focusable->set_hit_test_visible(true);
    non_focusable->set_layout_params({
        .width = 80.0f,
        .height = 32.0f,
        .margin = {.left = 140.0f},
    });
    root->add_child(std::move(non_focusable));
    runtime.layout(*root, {240.0f, 80.0f});
    std::array<View*, 1> roots{root.get()};

    runtime.begin_input_frame({
        .pointer_position = {10.0f, 10.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));
    ASSERT_TRUE(runtime.focused_text_input_bounds(*root).has_value());

    runtime.begin_input_frame({
        .pointer_position = {10.0f, 10.0f},
        .pointer_released = {true, false, false},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));

    runtime.begin_input_frame({
        .pointer_position = {160.0f, 10.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));
    EXPECT_EQ(focus_lost, 1);
    EXPECT_FALSE(runtime.focused_text_input_bounds(*root).has_value());
}

TEST(RetainedMui, AnimationCompletesAndSetsFinalValue) {
    float value = 0.0f;
    Animator animator;
    animator.add(Animation(0.0f, 10.0f, 1.0f, [&](float v) {
        value = v;
    }));

    animator.update(0.5f);
    EXPECT_GT(value, 0.0f);
    EXPECT_LT(value, 10.0f);
    EXPECT_FALSE(animator.empty());

    animator.update(0.5f);
    EXPECT_FLOAT_EQ(value, 10.0f);
    EXPECT_TRUE(animator.empty());
}

namespace {

namespace mod = snt::ui::mod;

class CapturingModUiCommandSink final : public mod::IModUiCommandSink {
public:
    snt::core::Expected<void> dispatch(mod::Command command) override {
        commands.push_back(std::move(command));
        return {};
    }

    std::vector<mod::Command> commands;
};

const mod::Command* find_mod_command(const std::vector<mod::Command>& commands,
                                     std::string_view name) {
    const auto found = std::find_if(commands.begin(), commands.end(),
        [name](const mod::Command& command) { return command.name == name; });
    return found == commands.end() ? nullptr : &*found;
}

}  // namespace

TEST(RetainedMui, FlexLayoutResolvesWeightsJustifyAndAlignment) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    auto weighted = std::make_unique<FlexLayout>("weighted");
    weighted->set_orientation(Orientation::Horizontal);
    weighted->set_align(FlexAlign::Center);
    auto first = std::make_unique<View>("first");
    auto second = std::make_unique<View>("second");
    View* raw_first = first.get();
    View* raw_second = second.get();
    first->set_layout_params({.width = 0.0f, .height = 20.0f, .weight = 1.0f});
    second->set_layout_params({.width = 0.0f, .height = 20.0f, .weight = 2.0f});
    weighted->add_child(std::move(first));
    weighted->add_child(std::move(second));
    runtime.layout(*weighted, {300.0f, 60.0f});

    EXPECT_FLOAT_EQ(raw_first->bounds().size.x, 100.0f);
    EXPECT_FLOAT_EQ(raw_second->bounds().size.x, 200.0f);
    EXPECT_FLOAT_EQ(raw_first->bounds().pos.x, 0.0f);
    EXPECT_FLOAT_EQ(raw_second->bounds().pos.x, 100.0f);
    EXPECT_FLOAT_EQ(raw_first->bounds().pos.y, 20.0f);
    EXPECT_FLOAT_EQ(raw_second->bounds().pos.y, 20.0f);

    auto spaced = std::make_unique<FlexLayout>("spaced");
    spaced->set_orientation(Orientation::Horizontal);
    spaced->set_justify(FlexJustify::SpaceBetween);
    auto left = std::make_unique<View>("left");
    auto right = std::make_unique<View>("right");
    View* raw_left = left.get();
    View* raw_right = right.get();
    left->set_layout_params({.width = 20.0f, .height = 20.0f});
    right->set_layout_params({.width = 20.0f, .height = 20.0f});
    spaced->add_child(std::move(left));
    spaced->add_child(std::move(right));
    runtime.layout(*spaced, {100.0f, 30.0f});

    EXPECT_FLOAT_EQ(raw_left->bounds().pos.x, 0.0f);
    EXPECT_FLOAT_EQ(raw_right->bounds().pos.x, 80.0f);
}

TEST(RetainedMui, NineSliceEmitsNineImagePatches) {
    UiImageRegistry images;
    ASSERT_TRUE(images.register_rgba("panel", 8, 8, std::vector<uint8_t>(8u * 8u * 4u, 255u)));

    Arc2DCommandBuffer commands;
    commands.nine_slice({.pos = {10.0f, 20.0f}, .size = {120.0f, 60.0f}}, "panel",
                        {.left = 2.0f, .top = 2.0f, .right = 2.0f, .bottom = 2.0f});
    Arc2DRenderer renderer(images);
    const UiDrawData data = renderer.build_draw_data(commands);

    EXPECT_EQ(data.vertices.size(), 36u);
    EXPECT_EQ(data.indices.size(), 54u);
    EXPECT_TRUE(std::all_of(data.vertices.begin(), data.vertices.end(),
        [](const UiVertex& vertex) { return vertex.texture_mode == UiTextureMode::Image; }));
}

TEST(RetainedMui, ViewportUsesOneScaleForInputAndDrawData) {
    const UiViewport viewport{
        .framebuffer_size = {2000.0f, 1000.0f},
        .window_size = {1000.0f, 500.0f},
        .dpi_scale = 2.0f,
        .user_scale = 1.25f,
    };
    EXPECT_FLOAT_EQ(viewport.pixels_per_ui_unit(), 2.5f);
    const Vec2 logical_size = viewport.logical_size();
    const Vec2 logical_point = viewport.window_to_logical({500.0f, 250.0f});
    const Vec2 window_point = viewport.logical_to_window({400.0f, 200.0f});
    EXPECT_FLOAT_EQ(logical_size.x, 800.0f);
    EXPECT_FLOAT_EQ(logical_size.y, 400.0f);
    EXPECT_FLOAT_EQ(logical_point.x, 400.0f);
    EXPECT_FLOAT_EQ(logical_point.y, 200.0f);
    EXPECT_FLOAT_EQ(window_point.x, 500.0f);
    EXPECT_FLOAT_EQ(window_point.y, 250.0f);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    runtime.set_viewport(viewport);
    auto root = std::make_unique<View>("scaled_root");
    root->set_background({40, 60, 80, 255});
    runtime.layout(*root, viewport.logical_size());
    const UiFrameResult frame = runtime.paint(*root);
    ASSERT_FALSE(frame.draw_data.vertices.empty());

    float maximum_x = 0.0f;
    float maximum_y = 0.0f;
    for (const UiVertex& vertex : frame.draw_data.vertices) {
        maximum_x = std::max(maximum_x, vertex.position[0]);
        maximum_y = std::max(maximum_y, vertex.position[1]);
    }
    EXPECT_FLOAT_EQ(maximum_x, 2000.0f);
    EXPECT_FLOAT_EQ(maximum_y, 1000.0f);
}

TEST(RetainedMui, TextInputReceivesImePreeditAndCommittedUtf8) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);

    auto root = std::make_unique<FrameLayout>("ime_root");
    auto editor = std::make_unique<TextInput>("editor");
    TextInput* raw_editor = editor.get();
    editor->set_layout_params({.width = 180.0f, .height = 32.0f});
    root->add_child(std::move(editor));
    runtime.layout(*root, {240.0f, 80.0f});

    runtime.begin_input_frame({
        .pointer_position = {10.0f, 10.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));
    runtime.begin_input_frame({
        .text_compositions = {{.text = "ni", .start = 0, .length = 2}},
    });
    ASSERT_TRUE(runtime.dispatch_keyboard_input(*root));
    const UiFrameResult preedit = runtime.paint(*root);
    EXPECT_TRUE(std::any_of(preedit.commands.commands().begin(), preedit.commands.commands().end(),
        [](const ArcDrawCommand& command) {
            const auto* text = std::get_if<DrawTextCommand>(&command);
            return text && text->text == "ni";
        }));

    runtime.begin_input_frame({
        .text_commits = {std::string("\\xE4\\xBD\\xA0")},
    });
    ASSERT_TRUE(runtime.dispatch_keyboard_input(*root));
    EXPECT_EQ(raw_editor->text(), std::string("\\xE4\\xBD\\xA0"));
}

TEST(RetainedMui, TextInputSelectionClipboardAndHistoryUseUtf8Boundaries) {
    TextInput input("name");
    input.set_text_silently(std::string("\xE4\xBD\xA0") + "A");
    input.on_input_event({.type = UiInputEventType::KeyDown, .key = UiKey::Home});
    input.on_input_event({.type = UiInputEventType::KeyDown,
                          .key = UiKey::Right,
                          .modifiers = UiKeyModifier::Shift});

    ASSERT_TRUE(input.has_selection());
    EXPECT_EQ(input.selected_text(), std::string("\xE4\xBD\xA0"));
    UiMemoryClipboard clipboard;
    ASSERT_TRUE(input.copy_selection(clipboard));
    EXPECT_EQ(clipboard.text(), std::string("\xE4\xBD\xA0"));

    ASSERT_TRUE(input.cut_selection(clipboard));
    EXPECT_EQ(input.text(), "A");
    EXPECT_TRUE(input.undo());
    EXPECT_EQ(input.text(), std::string("\xE4\xBD\xA0") + "A");
    EXPECT_TRUE(input.redo());
    EXPECT_EQ(input.text(), "A");

    ASSERT_TRUE(clipboard.write_text("Beta"));
    input.select_all();
    ASSERT_TRUE(input.paste_from(clipboard));
    EXPECT_EQ(input.text(), "Beta");
}

TEST(RetainedMui, RuntimeRoutesSemanticClipboardShortcutsAndImePlatform) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    const UiViewport viewport{
        .framebuffer_size = {2000.0f, 1000.0f},
        .window_size = {1000.0f, 500.0f},
        .dpi_scale = 2.0f,
        .user_scale = 1.25f,
    };
    runtime.set_viewport(viewport);
    auto clipboard = std::make_shared<UiMemoryClipboard>();
    auto platform = std::make_shared<RecordingUiTextInputPlatform>();
    runtime.set_clipboard(clipboard);
    runtime.set_text_input_platform(platform);

    auto root = std::make_unique<FrameLayout>("editor_root");
    auto input = std::make_unique<TextInput>("editor");
    TextInput* raw_input = input.get();
    input->set_layout_params({.width = 180.0f, .height = 32.0f});
    input->set_text_silently("old");
    root->add_child(std::move(input));
    runtime.layout(*root, viewport.logical_size());
    std::array<View*, 1> roots{root.get()};

    runtime.begin_input_frame({
        .pointer_position = {10.0f, 10.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_pointer_input(*root));
    runtime.synchronize_text_input_platform(roots);
    ASSERT_EQ(platform->activations, (std::vector<bool>{true}));
    ASSERT_EQ(platform->areas.size(), 1u);
    EXPECT_EQ(platform->areas.front().width, 225);
    EXPECT_EQ(platform->areas.front().height, 40);

    runtime.begin_input_frame({
        .modifiers = UiKeyModifier::Control,
        .pressed_keys = {UiKey::A},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_keyboard_input(*root));
    EXPECT_TRUE(raw_input->has_selection());
    runtime.begin_input_frame({
        .modifiers = UiKeyModifier::Control,
        .pressed_keys = {UiKey::C},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_keyboard_input(*root));
    EXPECT_EQ(clipboard->text(), "old");

    ASSERT_TRUE(clipboard->write_text("new"));
    runtime.begin_input_frame({
        .modifiers = UiKeyModifier::Control,
        .pressed_keys = {UiKey::V},
    }, roots);
    ASSERT_TRUE(runtime.dispatch_keyboard_input(*root));
    EXPECT_EQ(raw_input->text(), "new");

    runtime.clear_interaction_state(roots);
    runtime.synchronize_text_input_platform(roots);
    EXPECT_EQ(platform->activations, (std::vector<bool>{true, false}));
}

TEST(RetainedMui, InputRouterOwnsPointerFocusAndInteractionState) {
    UiInputRouter router;
    Button button("router_button");
    button.layout({.pos = {0.0f, 0.0f}, .size = {120.0f, 40.0f}});

    router.begin_frame({
        .pointer_position = {16.0f, 16.0f},
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    });
    ASSERT_TRUE(router.dispatch_pointer_input(button));
    router.synchronize_interaction_state(button);
    EXPECT_TRUE(has_interaction_state(button.interaction_state(), UiInteractionState::Pressed));
    EXPECT_TRUE(has_interaction_state(button.interaction_state(), UiInteractionState::Focused));

    router.begin_frame({
        .pointer_position = {16.0f, 16.0f},
        .pointer_released = {true, false, false},
    });
    ASSERT_TRUE(router.dispatch_pointer_input(button));
    router.synchronize_interaction_state(button);
    EXPECT_FALSE(has_interaction_state(button.interaction_state(), UiInteractionState::Pressed));
    EXPECT_TRUE(has_interaction_state(button.interaction_state(), UiInteractionState::Focused));
}

TEST(RetainedMui, TextInputServiceOwnsClipboardAndNativePlatformState) {
    UiTextInputService service;
    auto clipboard = std::make_shared<UiMemoryClipboard>();
    auto platform = std::make_shared<RecordingUiTextInputPlatform>();
    service.set_clipboard(clipboard);
    service.set_text_input_platform(platform);

    TextInput input("service_input");
    input.set_text_silently("old");
    input.select_all();
    EXPECT_TRUE(service.handle_clipboard_shortcut(
        input, UiKey::C, UiKeyModifier::Control, "service_root", input.id()));
    EXPECT_EQ(clipboard->text(), "old");

    ASSERT_TRUE(clipboard->write_text("new"));
    input.select_all();
    EXPECT_TRUE(service.handle_clipboard_shortcut(
        input, UiKey::V, UiKeyModifier::Control, "service_root", input.id()));
    EXPECT_EQ(input.text(), "new");

    const UiViewport viewport{
        .framebuffer_size = {2000.0f, 1000.0f},
        .window_size = {1000.0f, 500.0f},
        .dpi_scale = 2.0f,
        .user_scale = 1.25f,
    };
    service.synchronize_platform(viewport,
                                 Rect{.pos = {0.0f, 0.0f}, .size = {180.0f, 32.0f}},
                                 "service_root", input.id());
    ASSERT_EQ(platform->activations, (std::vector<bool>{true}));
    ASSERT_EQ(platform->areas.size(), 1u);
    EXPECT_EQ(platform->areas.front().width, 225);
    EXPECT_EQ(platform->areas.front().height, 40);

    service.synchronize_platform(viewport, std::nullopt, {}, {});
    EXPECT_EQ(platform->activations, (std::vector<bool>{true, false}));
}

TEST(RetainedMui, ModFacadeOwnsControlsModelsCommandsAndResources) {
    UiImageRegistry images;
    UiLayerStack layers;
    CapturingModUiCommandSink sink;
    auto host_result = mod::internal::create_mod_ui_host(
        {.value = "example_mod"}, layers, images, sink);
    ASSERT_TRUE(host_result) << host_result.error().format();
    std::unique_ptr<mod::IModUiHost> host = std::move(*host_result);

    ASSERT_TRUE(host->register_image({
        .ref = {.value = "icon"},
        .width = 4,
        .height = 4,
        .rgba = std::vector<uint8_t>(4u * 4u * 4u, 255u),
    }));
    ASSERT_TRUE(host->register_image({
        .ref = {.value = "panel"},
        .width = 4,
        .height = 4,
        .rgba = std::vector<uint8_t>(4u * 4u * 4u, 180u),
    }));

    mod::Widget controls_root;
    controls_root.type = mod::WidgetType::Flex;
    controls_root.id = {.value = "controls_root"};
    controls_root.layout.width = 0.0f;
    controls_root.layout.height = 0.0f;
    controls_root.layout.spacing = 4.0f;

    mod::Widget editor;
    editor.type = mod::WidgetType::TextInput;
    editor.id = {.value = "editor"};
    editor.layout.width = 220.0f;
    editor.layout.height = 32.0f;
    editor.placeholder = "Name";
    editor.view_model = {.value = "profile"};
    editor.value_key = "name";
    editor.actions.change.name = "profile.changed";
    editor.actions.submit.name = "profile.submitted";
    controls_root.children.push_back(std::move(editor));

    mod::Widget notes;
    notes.type = mod::WidgetType::TextEditor;
    notes.id = {.value = "notes"};
    notes.layout.width = 220.0f;
    notes.min_text_lines = 4;
    notes.placeholder = "Notes";
    notes.actions.change.name = "notes.changed";
    notes.actions.submit.name = "notes.submitted";
    controls_root.children.push_back(std::move(notes));

    mod::Widget checkbox;
    checkbox.type = mod::WidgetType::Checkbox;
    checkbox.id = {.value = "enabled"};
    checkbox.text = "Enabled";
    checkbox.view_model = {.value = "profile"};
    checkbox.value_key = "enabled";
    checkbox.actions.change.name = "enabled.changed";
    controls_root.children.push_back(std::move(checkbox));

    mod::Widget slider;
    slider.type = mod::WidgetType::Slider;
    slider.id = {.value = "volume"};
    slider.minimum = 0.0f;
    slider.maximum = 1.0f;
    slider.step = 0.25f;
    slider.value = 0.5f;
    slider.view_model = {.value = "profile"};
    slider.value_key = "volume";
    slider.actions.change.name = "volume.changed";
    controls_root.children.push_back(std::move(slider));

    mod::Widget button;
    button.type = mod::WidgetType::Button;
    button.id = {.value = "run"};
    button.text = "Run";
    button.actions.activate.name = "run.clicked";
    controls_root.children.push_back(std::move(button));

    mod::Widget icon;
    icon.type = mod::WidgetType::Image;
    icon.id = {.value = "icon_view"};
    icon.resource = {.value = "icon"};
    controls_root.children.push_back(std::move(icon));

    mod::Widget panel;
    panel.type = mod::WidgetType::NineSlice;
    panel.id = {.value = "panel_view"};
    panel.resource = {.value = "panel"};
    panel.nine_slice_borders = {.left = 1.0f, .top = 1.0f, .right = 1.0f, .bottom = 1.0f};
    controls_root.children.push_back(std::move(panel));

    mod::Widget slot;
    slot.type = mod::WidgetType::Slot;
    slot.id = {.value = "inventory_slot"};
    slot.slot = {.item = {.value = "icon"}, .count = 3};
    slot.actions.drop.name = "inventory.drop";
    controls_root.children.push_back(std::move(slot));

    mod::Widget list;
    list.type = mod::WidgetType::VirtualList;
    list.id = {.value = "items"};
    list.layout.width = 220.0f;
    list.layout.height = 48.0f;
    list.virtual_item_count = 100;
    list.virtual_item_extent = 20.0f;
    mod::Widget row;
    row.type = mod::WidgetType::Text;
    row.id = {.value = "row"};
    row.text = "Item";
    list.children.push_back(std::move(row));
    controls_root.children.push_back(std::move(list));

    mod::Screen controls;
    controls.id = {.value = "controls"};
    controls.initially_visible = true;
    controls.root = std::move(controls_root);

    mod::Screen modal;
    modal.id = {.value = "dialog"};
    modal.layer = mod::Layer::Modal;
    modal.initially_visible = true;
    modal.root.type = mod::WidgetType::Modal;
    modal.root.id = {.value = "dialog_root"};
    modal.root.actions.dismiss.name = "dialog.dismiss";
    mod::Widget modal_text;
    modal_text.type = mod::WidgetType::Text;
    modal_text.id = {.value = "dialog_text"};
    modal_text.text = "Dialog";
    modal.root.children.push_back(std::move(modal_text));

    mod::Screen tooltip;
    tooltip.id = {.value = "tip"};
    tooltip.layer = mod::Layer::Tooltip;
    tooltip.initially_visible = true;
    tooltip.root.type = mod::WidgetType::Tooltip;
    tooltip.root.id = {.value = "tip_root"};
    tooltip.root.text = "Tip";

    ASSERT_TRUE(host->replace_screens({std::move(controls), std::move(modal), std::move(tooltip)}));
    const auto& submissions = layers.prepare_frame({.viewport = {320.0f, 200.0f}, .images = images});
    ASSERT_EQ(submissions.size(), 3u);

    const auto controls_submission = std::find_if(submissions.begin(), submissions.end(),
        [](const UiScreenSubmission& submission) { return submission.layer == UiLayer::Screen; });
    ASSERT_NE(controls_submission, submissions.end());
    auto* controls_group = dynamic_cast<ViewGroup*>(controls_submission->root);
    ASSERT_NE(controls_group, nullptr);
    auto* retained_editor = dynamic_cast<TextInput*>(controls_group->find("editor"));
    auto* retained_notes = dynamic_cast<TextEditor*>(controls_group->find("notes"));
    auto* retained_checkbox = dynamic_cast<Checkbox*>(controls_group->find("enabled"));
    auto* retained_slider = dynamic_cast<Slider*>(controls_group->find("volume"));
    auto* retained_button = dynamic_cast<Button*>(controls_group->find("run"));
    auto* retained_slot = dynamic_cast<SlotView*>(controls_group->find("inventory_slot"));
    auto* retained_list = dynamic_cast<VirtualListView*>(controls_group->find("items"));
    EXPECT_NE(dynamic_cast<ImageView*>(controls_group->find("icon_view")), nullptr);
    EXPECT_NE(dynamic_cast<NineSliceView*>(controls_group->find("panel_view")), nullptr);
    ASSERT_NE(retained_editor, nullptr);
    ASSERT_NE(retained_notes, nullptr);
    ASSERT_NE(retained_checkbox, nullptr);
    ASSERT_NE(retained_slider, nullptr);
    ASSERT_NE(retained_button, nullptr);
    ASSERT_NE(retained_slot, nullptr);
    ASSERT_NE(retained_list, nullptr);
    EXPECT_EQ(retained_notes->min_visible_lines(), 4u);
    EXPECT_EQ(retained_list->item_count(), 100u);

    const auto modal_submission = std::find_if(submissions.begin(), submissions.end(),
        [](const UiScreenSubmission& submission) { return submission.layer == UiLayer::Modal; });
    ASSERT_NE(modal_submission, submissions.end());
    EXPECT_EQ(modal_submission->root->kind(), ViewKind::Modal);
    const auto tooltip_submission = std::find_if(submissions.begin(), submissions.end(),
        [](const UiScreenSubmission& submission) { return submission.layer == UiLayer::Tooltip; });
    ASSERT_NE(tooltip_submission, submissions.end());
    EXPECT_EQ(tooltip_submission->root->kind(), ViewKind::Tooltip);

    retained_editor->on_input_event({.type = UiInputEventType::TextCommit, .text = "Ada"});
    retained_editor->on_input_event({.type = UiInputEventType::KeyDown, .key = UiKey::Enter});
    retained_notes->on_input_event({.type = UiInputEventType::TextCommit, .text = "Line"});
    retained_notes->on_input_event({.type = UiInputEventType::KeyDown, .key = UiKey::Enter});
    retained_notes->on_input_event({.type = UiInputEventType::KeyDown,
                                    .key = UiKey::Enter,
                                    .modifiers = UiKeyModifier::Control});
    retained_checkbox->on_input_event({.type = UiInputEventType::PointerUp,
                                       .pointer_button = UiPointerButton::Primary,
                                       .activation = true});
    retained_slider->set_value(0.75f, true);
    EXPECT_TRUE(retained_button->activate());
    retained_slot->on_drag_event({
        .type = UiDragEventType::Drop,
        .source_id = "other_slot",
        .target_id = "inventory_slot",
        .payload = {.type = "snt.item", .resource_key = "example_mod:icon", .count = 3},
    });

    ASSERT_NE(find_mod_command(sink.commands, "profile.changed"), nullptr);
    ASSERT_NE(find_mod_command(sink.commands, "profile.submitted"), nullptr);
    ASSERT_NE(find_mod_command(sink.commands, "notes.changed"), nullptr);
    ASSERT_NE(find_mod_command(sink.commands, "notes.submitted"), nullptr);
    ASSERT_NE(find_mod_command(sink.commands, "enabled.changed"), nullptr);
    ASSERT_NE(find_mod_command(sink.commands, "volume.changed"), nullptr);
    ASSERT_NE(find_mod_command(sink.commands, "run.clicked"), nullptr);
    const mod::Command* drop = find_mod_command(sink.commands, "inventory.drop");
    ASSERT_NE(drop, nullptr);
    EXPECT_EQ(drop->screen.value, "controls");
    EXPECT_EQ(drop->widget.value, "inventory_slot");
    EXPECT_EQ(drop->related_widget.value, "other_slot");
    EXPECT_EQ(drop->slot.item.value, "icon");
    EXPECT_EQ(drop->slot.count, 3);

    ASSERT_TRUE(host->set_view_model_value({.value = "profile"}, "name", std::string("Grace")));
    ASSERT_TRUE(host->set_view_model_value({.value = "profile"}, "enabled", true));
    ASSERT_TRUE(host->set_view_model_value({.value = "profile"}, "volume", 0.25));
    EXPECT_EQ(retained_editor->text(), "Grace");
    EXPECT_TRUE(retained_checkbox->checked());
    EXPECT_FLOAT_EQ(retained_slider->value(), 0.25f);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    runtime.layout(*controls_submission->root, {320.0f, 200.0f});
    EXPECT_LT(retained_list->children().size(), retained_list->item_count());

    ASSERT_TRUE(host->unregister_owner());
    EXPECT_TRUE(layers.prepare_frame({.viewport = {320.0f, 200.0f}, .images = images}).empty());
    EXPECT_EQ(images.image_count(), 0u);
}

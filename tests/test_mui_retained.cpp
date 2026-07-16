#include "core/path_utils.h"
#include "ui/retained_mui.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace snt::ui;

snt::core::Expected<snt::core::RuntimePathResolver> make_test_path_resolver() {
    return snt::core::RuntimePathResolver::create({
        .engine_root = SNT_ENGINE_TEST_ROOT,
        .game_root = SNT_ENGINE_TEST_ROOT,
        .user_root = SNT_ENGINE_TEST_ROOT,
    });
}

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
    Arc2DRenderer renderer;
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

    Arc2DRenderer renderer;
    const UiDrawData draw_data = renderer.build_draw_data(commands);
    EXPECT_EQ(draw_data.vertices.size(), 17u);
    EXPECT_EQ(draw_data.indices.size(), 48u);
    for (const UiVertex& vertex : draw_data.vertices) {
        EXPECT_EQ(vertex.texture_mode, UiTextureMode::Solid);
    }
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

#include "ui/game_ui.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace snt::ui;

bool has_text_command(const Arc2DCommandBuffer& buffer, std::string_view needle) {
    for (const auto& command : buffer.commands()) {
        if (const auto* text = std::get_if<DrawTextCommand>(&command)) {
            if (text->text.find(needle) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

const DrawTextCommand* first_text_command(const Arc2DCommandBuffer& buffer,
                                          std::string_view needle) {
    for (const auto& command : buffer.commands()) {
        if (const auto* text = std::get_if<DrawTextCommand>(&command)) {
            if (text->text.find(needle) != std::string::npos) {
                return text;
            }
        }
    }
    return nullptr;
}

}  // namespace

TEST(RetainedMui, TextEngineShapesChineseAndEmoji) {
    UnicodeTextEngine engine;
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
    UnicodeTextEngine engine;
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

TEST(RetainedMui, MixedBidiAndJoinedEmojiProduceGlyphs) {
    UnicodeTextEngine engine;
    ASSERT_TRUE(engine.available()) << engine.initialization_error();

    TextStyle style;
    style.size_px = 20.0f;
    TextLayout layout = engine.shape("玩家 abc العربية 👩‍🚀", style);

    EXPECT_TRUE(layout.contains_cjk);
    EXPECT_TRUE(layout.contains_emoji);
    EXPECT_FALSE(layout.glyphs.empty());
    EXPECT_GT(layout.size.x, 0.0f);
}

TEST(RetainedMui, ViewModelBindingUpdatesTextView) {
    ViewModel model;
    auto text = std::make_unique<TextView>("title");
    TextView* raw = text.get();
    raw->bind_text(model, "screen.title");

    model.set("screen.title", std::string("合成 Crafting ⚒"));

    EXPECT_EQ(raw->text(), "合成 Crafting ⚒");
}

TEST(RetainedMui, InventoryScreenOpensAndRendersThroughArc2D) {
    GameplayUiController controller{
        InventoryViewModel{make_p6_demo_inventory()},
        make_p6_demo_recipes(),
    };
    controller.open_inventory();

    auto root = build_gameplay_ui_root(controller, {1280.0f, 720.0f});
    ASSERT_NE(root->find("hotbar"), nullptr);
    ASSERT_NE(root->find("inventory_panel"), nullptr);

    UiRuntime runtime;
    UiFrameResult frame = runtime.build_frame(*root, {1280.0f, 720.0f});

    EXPECT_TRUE(has_text_command(frame.commands, "背包"));
    EXPECT_FALSE(frame.draw_data.vertices.empty());

    const DrawTextCommand* title = first_text_command(frame.commands, "背包");
    ASSERT_NE(title, nullptr);
    EXPECT_TRUE(title->layout.contains_cjk);
    EXPECT_TRUE(title->layout.contains_emoji);
}

TEST(RetainedMui, PerformancePanelUsesRetainedViewModel) {
    PerformanceViewModel model;
    model.publish({
        .fps = 72.5f,
        .frame_ms = 13.79f,
        .tps = 20.0f,
        .mspt = 4.25f,
        .job_workers = 8,
    });

    auto panel = build_performance_panel_view(model);
    ASSERT_NE(panel->find("performance_panel"), nullptr);

    UiRuntime runtime;
    UiFrameResult frame = runtime.build_frame(*panel, {1280.0f, 720.0f});

    EXPECT_TRUE(has_text_command(frame.commands, "性能"));
    EXPECT_TRUE(has_text_command(frame.commands, "FPS"));
    EXPECT_TRUE(has_text_command(frame.commands, "72.5"));
    EXPECT_TRUE(has_text_command(frame.commands, "Job Workers"));
    EXPECT_FALSE(frame.draw_data.vertices.empty());
}

TEST(RetainedMui, CraftingScreenConsumesInputsAndProducesOutput) {
    GameplayUiController controller{
        InventoryViewModel{make_p6_demo_inventory()},
        make_p6_demo_recipes(),
    };
    controller.open_crafting();

    ASSERT_EQ(controller.inventory().count_item("plank.oak"), 8);
    CraftedItemResult result = controller.crafting().craft("craft_workbench");

    ASSERT_TRUE(result.ok) << result.reason;
    EXPECT_EQ(result.output.item_key, "workbench");
    EXPECT_EQ(controller.inventory().count_item("plank.oak"), 4);
    EXPECT_EQ(controller.inventory().count_item("workbench"), 1);

    auto root = build_gameplay_ui_root(controller, {1280.0f, 720.0f});
    ASSERT_NE(root->find("crafting_panel"), nullptr);

    UiRuntime runtime;
    UiFrameResult frame = runtime.build_frame(*root, {1280.0f, 720.0f});

    EXPECT_TRUE(has_text_command(frame.commands, "合成"));
    EXPECT_TRUE(has_text_command(frame.commands, "可合成"));

    const DrawTextCommand* title = first_text_command(frame.commands, "合成");
    ASSERT_NE(title, nullptr);
    EXPECT_TRUE(title->layout.contains_cjk);
    EXPECT_TRUE(title->layout.contains_emoji);
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

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

// Retained-MUI Arc2D draw-command and lowering interface.

#pragma once

#include "retained_mui_text.h"
#include "ui_image_registry.h"

#include <string>
#include <variant>
#include <vector>

namespace snt::ui {

struct DrawTextCommand {
    Rect rect{};
    std::string text;
    TextStyle style{};
    TextLayout layout{};
};

struct DrawRectCommand {
    Rect rect{};
    Color color{};
    float radius = 0.0f;
};

struct DrawImageCommand {
    Rect rect{};
    std::string image_key;
    Color tint{255, 255, 255, 255};
};

// Image-space border widths for a classic nine-slice. The renderer preserves
// all four borders and stretches only the center/edge spans.
struct DrawNineSliceCommand {
    Rect rect{};
    std::string image_key;
    Insets borders{};
    Color tint{255, 255, 255, 255};
};

struct PushClipCommand {
    Rect rect{};
};

struct PopClipCommand {};

using ArcDrawCommand = std::variant<DrawRectCommand, DrawTextCommand, DrawImageCommand,
                                    DrawNineSliceCommand, PushClipCommand, PopClipCommand>;

class Arc2DCommandBuffer {
public:
    void clear();
    void rect(Rect rect, Color color, float radius = 0.0f);
    void text(Rect rect, std::string text, TextStyle style, TextLayout layout);
    void image(Rect rect, std::string image_key, Color tint = {});
    void nine_slice(Rect rect, std::string image_key, Insets borders, Color tint = {});
    void push_clip(Rect rect);
    void pop_clip();

    const std::vector<ArcDrawCommand>& commands() const { return commands_; }

private:
    std::vector<ArcDrawCommand> commands_;
};

class Arc2DRenderer {
public:
    // Converts Arc2D primitives into renderer batches. Text is lowered from
    // HarfBuzz/FreeType glyphs, images resolve through the explicit registry,
    // and nested clips become dynamic Vulkan scissor state.
    explicit Arc2DRenderer(UiImageRegistry& images) : images_(&images) {}

    UiDrawData build_draw_data(const Arc2DCommandBuffer& commands) const;

private:
    static void append_rect(UiDrawData& out, Rect rect, Color color, float radius,
                            UiClipRect clip);
    static void append_glyph_text(UiDrawData& out, const DrawTextCommand& text,
                                  UiClipRect clip);
    void append_image(UiDrawData& out, const DrawImageCommand& image,
                      UiClipRect clip) const;
    void append_nine_slice(UiDrawData& out, const DrawNineSliceCommand& image,
                           UiClipRect clip) const;

    UiImageRegistry* images_ = nullptr;
};

}  // namespace snt::ui

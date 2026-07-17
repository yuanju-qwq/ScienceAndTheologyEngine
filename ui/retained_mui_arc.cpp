// Retained-MUI Arc2D command lowering.
//
// This module owns command recording and conversion into renderer draw data.
// It deliberately does not know about the retained view tree or input routing.

#define SNT_LOG_CHANNEL "ui.arc"
#include "retained_mui_arc.h"

#include "core/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

namespace snt::ui {
namespace {

bool is_non_empty_rect(Rect rect) {
    return rect.size.x > 0.0f && rect.size.y > 0.0f;
}

UiClipRect intersect_clip(UiClipRect current, Rect next) {
    if (!current.enabled) return {.enabled = true, .rect = next};
    const float left = std::max(current.rect.pos.x, next.pos.x);
    const float top = std::max(current.rect.pos.y, next.pos.y);
    const float right = std::min(current.rect.pos.x + current.rect.size.x,
                                 next.pos.x + next.size.x);
    const float bottom = std::min(current.rect.pos.y + current.rect.size.y,
                                  next.pos.y + next.size.y);
    return {
        .enabled = true,
        .rect = {
            .pos = {left, top},
            .size = {std::max(0.0f, right - left), std::max(0.0f, bottom - top)},
        },
    };
}

bool same_clip(const UiClipRect& left, const UiClipRect& right) {
    return left.enabled == right.enabled &&
           (!left.enabled ||
            (left.rect.pos.x == right.rect.pos.x &&
             left.rect.pos.y == right.rect.pos.y &&
             left.rect.size.x == right.rect.size.x &&
             left.rect.size.y == right.rect.size.y));
}

bool begin_draw_batch(UiDrawData& out, UiTextureBinding texture, UiClipRect clip) {
    if (clip.enabled && !is_non_empty_rect(clip.rect)) return false;
    if (!out.batches.empty()) {
        UiDrawBatch& last = out.batches.back();
        if (last.texture == texture && same_clip(last.clip, clip) &&
            last.first_index + last.index_count == out.indices.size()) {
            return true;
        }
    }
    out.batches.push_back({
        .first_index = static_cast<uint32_t>(out.indices.size()),
        .index_count = 0,
        .texture = texture,
        .clip = clip,
    });
    return true;
}

void append_batch_indices(UiDrawData& out, std::initializer_list<UiIndex> indices) {
    out.indices.insert(out.indices.end(), indices.begin(), indices.end());
    out.batches.back().index_count += static_cast<uint32_t>(indices.size());
}

bool has_vertex_capacity(const UiDrawData& out, size_t required) {
    return out.vertices.size() <=
        static_cast<size_t>(std::numeric_limits<UiIndex>::max()) - required;
}


}  // namespace

void Arc2DCommandBuffer::clear() {
    commands_.clear();
}

void Arc2DCommandBuffer::rect(Rect rect, Color color, float radius) {
    commands_.push_back(DrawRectCommand{rect, color, radius});
}

void Arc2DCommandBuffer::text(Rect rect, std::string text, TextStyle style, TextLayout layout) {
    commands_.push_back(DrawTextCommand{rect, std::move(text), style, std::move(layout)});
}

void Arc2DCommandBuffer::image(Rect rect, std::string image_key, Color tint) {
    commands_.push_back(DrawImageCommand{rect, std::move(image_key), tint});
}

void Arc2DCommandBuffer::nine_slice(Rect rect,
                                     std::string image_key,
                                     Insets borders,
                                     Color tint) {
    commands_.push_back(DrawNineSliceCommand{rect, std::move(image_key), borders, tint});
}

void Arc2DCommandBuffer::push_clip(Rect rect) {
    commands_.push_back(PushClipCommand{rect});
}

void Arc2DCommandBuffer::pop_clip() {
    commands_.push_back(PopClipCommand{});
}

UiDrawData Arc2DRenderer::build_draw_data(const Arc2DCommandBuffer& commands) const {
    UiDrawData out;
    std::vector<UiClipRect> clip_stack;
    UiClipRect current_clip{};
    bool logged_unbalanced_pop = false;
    for (const auto& cmd : commands.commands()) {
        if (const auto* rect = std::get_if<DrawRectCommand>(&cmd)) {
            append_rect(out, rect->rect, rect->color, rect->radius, current_clip);
        } else if (const auto* image = std::get_if<DrawImageCommand>(&cmd)) {
            append_image(out, *image, current_clip);
        } else if (const auto* nine_slice = std::get_if<DrawNineSliceCommand>(&cmd)) {
            append_nine_slice(out, *nine_slice, current_clip);
        } else if (const auto* text = std::get_if<DrawTextCommand>(&cmd)) {
            append_glyph_text(out, *text, current_clip);
        } else if (const auto* push = std::get_if<PushClipCommand>(&cmd)) {
            clip_stack.push_back(current_clip);
            current_clip = intersect_clip(current_clip, push->rect);
        } else if (std::holds_alternative<PopClipCommand>(cmd)) {
            if (clip_stack.empty()) {
                if (!logged_unbalanced_pop) {
                    SNT_LOG_WARN("Arc2D clip stack underflow; ignoring pop command");
                    logged_unbalanced_pop = true;
                }
            } else {
                current_clip = clip_stack.back();
                clip_stack.pop_back();
            }
        }
    }
    if (!clip_stack.empty()) {
        SNT_LOG_WARN("Arc2D clip stack ended with %zu unclosed clip region(s)", clip_stack.size());
    }
    return out;
}

void Arc2DRenderer::append_rect(UiDrawData& out,
                                Rect rect,
                                Color color,
                                float radius,
                                UiClipRect clip) {
    if (rect.size.x <= 0.0f || rect.size.y <= 0.0f) return;
    const auto append_vertex = [&out, color](Vec2 position) {
        UiVertex vertex{};
        vertex.position[0] = position.x;
        vertex.position[1] = position.y;
        vertex.uv[0] = -1.0f;
        vertex.uv[1] = -1.0f;
        vertex.color[0] = color.r;
        vertex.color[1] = color.g;
        vertex.color[2] = color.b;
        vertex.color[3] = color.a;
        out.vertices.push_back(vertex);
    };

    const float safe_radius = std::clamp(radius, 0.0f,
                                         std::min(rect.size.x, rect.size.y) * 0.5f);
    if (safe_radius <= 0.0f) {
        if (!has_vertex_capacity(out, 4)) {
            SNT_LOG_WARN("Arc2DRenderer draw buffer overflow; dropping rect");
            return;
        }
        if (!begin_draw_batch(out, UiTextureBinding::GlyphAtlas, clip)) return;
        const UiIndex base = static_cast<UiIndex>(out.vertices.size());
        const float x0 = rect.pos.x;
        const float y0 = rect.pos.y;
        const float x1 = rect.pos.x + rect.size.x;
        const float y1 = rect.pos.y + rect.size.y;
        append_vertex({x0, y0});
        append_vertex({x0, y1});
        append_vertex({x1, y1});
        append_vertex({x1, y0});
        append_batch_indices(out, {
            base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
        });
        return;
    }

    // Four segments per corner make compact game UI corners smooth without
    // turning panels and slot grids into a large vertex stream.
    constexpr size_t kSegmentsPerCorner = 4;
    constexpr size_t kPerimeterCount = kSegmentsPerCorner * 4;
    constexpr float kPi = 3.14159265358979323846f;
    if (!has_vertex_capacity(out, 1 + kPerimeterCount)) {
        SNT_LOG_WARN("Arc2DRenderer draw buffer overflow; dropping rounded rect");
        return;
    }

    if (!begin_draw_batch(out, UiTextureBinding::GlyphAtlas, clip)) return;
    const UiIndex base = static_cast<UiIndex>(out.vertices.size());
    append_vertex({rect.pos.x + rect.size.x * 0.5f, rect.pos.y + rect.size.y * 0.5f});
    const std::array<Vec2, 4> centers{{
        {rect.pos.x + safe_radius, rect.pos.y + safe_radius},
        {rect.pos.x + rect.size.x - safe_radius, rect.pos.y + safe_radius},
        {rect.pos.x + rect.size.x - safe_radius, rect.pos.y + rect.size.y - safe_radius},
        {rect.pos.x + safe_radius, rect.pos.y + rect.size.y - safe_radius},
    }};
    const std::array<float, 4> starts{{kPi, kPi * 1.5f, 0.0f, kPi * 0.5f}};
    for (size_t corner = 0; corner < centers.size(); ++corner) {
        for (size_t segment = 0; segment < kSegmentsPerCorner; ++segment) {
            const float fraction = static_cast<float>(segment) /
                static_cast<float>(kSegmentsPerCorner);
            const float angle = starts[corner] + fraction * (kPi * 0.5f);
            append_vertex({
                centers[corner].x + std::cos(angle) * safe_radius,
                centers[corner].y + std::sin(angle) * safe_radius,
            });
        }
    }
    for (UiIndex index = 0; index < kPerimeterCount; ++index) {
        const UiIndex next = (index + 1) % kPerimeterCount;
        append_batch_indices(out, {base, base + 1 + next, base + 1 + index});
    }
}

void Arc2DRenderer::append_glyph_text(UiDrawData& out,
                                      const DrawTextCommand& text,
                                      UiClipRect clip) {
    if (!text.layout.glyph_atlas) {
        SNT_LOG_ERROR("MUI text command is missing its Unicode glyph atlas");
        return;
    }
    if (!out.glyph_atlas) {
        out.glyph_atlas = text.layout.glyph_atlas;
    } else if (out.glyph_atlas.get() != text.layout.glyph_atlas.get()) {
        SNT_LOG_ERROR("MUI frame combines text from different glyph atlases; rejecting batch");
        return;
    }

    for (const TextGlyph& glyph : text.layout.glyphs) {
        if (!glyph.drawable || glyph.width <= 0.0f || glyph.height <= 0.0f) continue;
        if (!has_vertex_capacity(out, 4)) {
            SNT_LOG_WARN("Arc2DRenderer draw buffer overflow; dropping glyph batch");
            return;
        }
        if (!begin_draw_batch(out, UiTextureBinding::GlyphAtlas, clip)) return;

        const UiIndex base = static_cast<UiIndex>(out.vertices.size());
        const Color color = glyph.color ? Color{255, 255, 255, 255} : text.style.color;
        UiVertex vertex{};
        vertex.color[0] = color.r;
        vertex.color[1] = color.g;
        vertex.color[2] = color.b;
        vertex.color[3] = color.a;
        vertex.texture_mode = glyph.color
            ? UiTextureMode::ColorGlyph
            : UiTextureMode::SignedDistanceGlyph;

        const float x0 = text.rect.pos.x + glyph.x;
        const float y0 = text.rect.pos.y + glyph.y;
        const float x1 = x0 + glyph.width;
        const float y1 = y0 + glyph.height;
        vertex.position[0] = x0; vertex.position[1] = y0;
        vertex.uv[0] = glyph.u0; vertex.uv[1] = glyph.v0; out.vertices.push_back(vertex);
        vertex.position[0] = x0; vertex.position[1] = y1;
        vertex.uv[0] = glyph.u0; vertex.uv[1] = glyph.v1; out.vertices.push_back(vertex);
        vertex.position[0] = x1; vertex.position[1] = y1;
        vertex.uv[0] = glyph.u1; vertex.uv[1] = glyph.v1; out.vertices.push_back(vertex);
        vertex.position[0] = x1; vertex.position[1] = y0;
        vertex.uv[0] = glyph.u1; vertex.uv[1] = glyph.v0; out.vertices.push_back(vertex);

        append_batch_indices(out, {
            base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
        });
    }
}

void Arc2DRenderer::append_image(UiDrawData& out,
                                 const DrawImageCommand& image,
                                 UiClipRect clip) const {
    if (!images_ || !is_non_empty_rect(image.rect)) return;
    const UiImageRegion* region = images_->resolve(image.image_key);
    if (!region) return;
    const auto atlas = images_->atlas();
    if (!atlas) {
        SNT_LOG_ERROR("MUI image registry resolved '%s' without an atlas", image.image_key.c_str());
        return;
    }
    if (!out.image_atlas) {
        out.image_atlas = atlas;
    } else if (out.image_atlas.get() != atlas.get()) {
        SNT_LOG_ERROR("MUI frame combines images from different UI atlases; rejecting image batch");
        return;
    }
    if (!has_vertex_capacity(out, 4)) {
        SNT_LOG_WARN("Arc2DRenderer draw buffer overflow; dropping image batch");
        return;
    }
    if (!begin_draw_batch(out, UiTextureBinding::ImageAtlas, clip)) return;

    const UiIndex base = static_cast<UiIndex>(out.vertices.size());
    UiVertex vertex{};
    vertex.color[0] = image.tint.r;
    vertex.color[1] = image.tint.g;
    vertex.color[2] = image.tint.b;
    vertex.color[3] = image.tint.a;
    vertex.texture_mode = UiTextureMode::Image;

    const float x0 = image.rect.pos.x;
    const float y0 = image.rect.pos.y;
    const float x1 = x0 + image.rect.size.x;
    const float y1 = y0 + image.rect.size.y;
    vertex.position[0] = x0; vertex.position[1] = y0;
    vertex.uv[0] = region->u0; vertex.uv[1] = region->v0; out.vertices.push_back(vertex);
    vertex.position[0] = x0; vertex.position[1] = y1;
    vertex.uv[0] = region->u0; vertex.uv[1] = region->v1; out.vertices.push_back(vertex);
    vertex.position[0] = x1; vertex.position[1] = y1;
    vertex.uv[0] = region->u1; vertex.uv[1] = region->v1; out.vertices.push_back(vertex);
    vertex.position[0] = x1; vertex.position[1] = y0;
    vertex.uv[0] = region->u1; vertex.uv[1] = region->v0; out.vertices.push_back(vertex);
    append_batch_indices(out, {
        base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
    });
}

void Arc2DRenderer::append_nine_slice(UiDrawData& out,
                                      const DrawNineSliceCommand& image,
                                      UiClipRect clip) const {
    if (!images_ || !is_non_empty_rect(image.rect)) return;
    const UiImageRegion* region = images_->resolve(image.image_key);
    if (!region || region->width == 0 || region->height == 0) return;
    const auto atlas = images_->atlas();
    if (!atlas) {
        SNT_LOG_ERROR("MUI image registry resolved nine-slice '%s' without an atlas",
                      image.image_key.c_str());
        return;
    }
    if (!out.image_atlas) {
        out.image_atlas = atlas;
    } else if (out.image_atlas.get() != atlas.get()) {
        SNT_LOG_ERROR("MUI frame combines images from different UI atlases; rejecting nine-slice");
        return;
    }

    const float source_width = static_cast<float>(region->width);
    const float source_height = static_cast<float>(region->height);
    const float source_left = std::clamp(image.borders.left, 0.0f, source_width * 0.5f);
    const float source_right = std::clamp(image.borders.right, 0.0f,
                                          source_width - source_left);
    const float source_top = std::clamp(image.borders.top, 0.0f, source_height * 0.5f);
    const float source_bottom = std::clamp(image.borders.bottom, 0.0f,
                                           source_height - source_top);

    const float dest_left = std::min(source_left, image.rect.size.x * 0.5f);
    const float dest_right = std::min(source_right, image.rect.size.x - dest_left);
    const float dest_top = std::min(source_top, image.rect.size.y * 0.5f);
    const float dest_bottom = std::min(source_bottom, image.rect.size.y - dest_top);
    const std::array<float, 4> xs{{image.rect.pos.x,
                                   image.rect.pos.x + dest_left,
                                   image.rect.pos.x + image.rect.size.x - dest_right,
                                   image.rect.pos.x + image.rect.size.x}};
    const std::array<float, 4> ys{{image.rect.pos.y,
                                   image.rect.pos.y + dest_top,
                                   image.rect.pos.y + image.rect.size.y - dest_bottom,
                                   image.rect.pos.y + image.rect.size.y}};
    const float u_span = region->u1 - region->u0;
    const float v_span = region->v1 - region->v0;
    const std::array<float, 4> us{{region->u0,
                                   region->u0 + u_span * source_left / source_width,
                                   region->u1 - u_span * source_right / source_width,
                                   region->u1}};
    const std::array<float, 4> vs{{region->v0,
                                   region->v0 + v_span * source_top / source_height,
                                   region->v1 - v_span * source_bottom / source_height,
                                   region->v1}};

    const auto append_patch = [&out, clip, &image](float x0, float y0, float x1, float y1,
                                                    float u0, float v0, float u1, float v1) {
        if (x1 <= x0 || y1 <= y0 || !has_vertex_capacity(out, 4)) return;
        if (!begin_draw_batch(out, UiTextureBinding::ImageAtlas, clip)) return;
        const UiIndex base = static_cast<UiIndex>(out.vertices.size());
        UiVertex vertex{};
        vertex.color[0] = image.tint.r;
        vertex.color[1] = image.tint.g;
        vertex.color[2] = image.tint.b;
        vertex.color[3] = image.tint.a;
        vertex.texture_mode = UiTextureMode::Image;
        vertex.position[0] = x0; vertex.position[1] = y0;
        vertex.uv[0] = u0; vertex.uv[1] = v0; out.vertices.push_back(vertex);
        vertex.position[0] = x0; vertex.position[1] = y1;
        vertex.uv[0] = u0; vertex.uv[1] = v1; out.vertices.push_back(vertex);
        vertex.position[0] = x1; vertex.position[1] = y1;
        vertex.uv[0] = u1; vertex.uv[1] = v1; out.vertices.push_back(vertex);
        vertex.position[0] = x1; vertex.position[1] = y0;
        vertex.uv[0] = u1; vertex.uv[1] = v0; out.vertices.push_back(vertex);
        append_batch_indices(out, {
            base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
        });
    };

    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            append_patch(xs[column], ys[row], xs[column + 1], ys[row + 1],
                         us[column], vs[row], us[column + 1], vs[row + 1]);
        }
    }
}


}  // namespace snt::ui
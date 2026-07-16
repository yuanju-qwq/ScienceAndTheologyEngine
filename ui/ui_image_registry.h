// Dynamic UI image registry and atlas for retained MUI.
//
// This module is UI-main-thread-owned. Hosts and mods register stable image
// keys with an explicit file path or RGBA payload; Arc2D resolves those keys
// into one revisioned atlas that MuiRenderer uploads with the frame.

#pragma once

#include "assets/texture_cache.h"
#include "core/expected.h"
#include "ui_draw_data.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace snt::ui {

struct UiImageRegion {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    uint32_t width = 0;
    uint32_t height = 0;
};

class UiImageRegistry final {
public:
    UiImageRegistry();

    UiImageRegistry(const UiImageRegistry&) = delete;
    UiImageRegistry& operator=(const UiImageRegistry&) = delete;

    // Registers a logical image key backed by an explicit filesystem path.
    // Re-registering the same key/path is idempotent; a different-size image
    // for an existing key is rejected because its atlas UVs are in flight.
    [[nodiscard]] snt::core::Expected<void> register_file(
        std::string key,
        std::string path);

    // Registers or updates an RGBA payload. This is the mod-safe extension
    // point for generated icons and package systems that do not expose files.
    [[nodiscard]] snt::core::Expected<void> register_rgba(
        std::string key,
        uint32_t width,
        uint32_t height,
        std::vector<uint8_t> rgba);

    // Unknown keys resolve to the configured fallback if present. A missing
    // key is logged once so a bad content mapping is diagnosable without
    // producing per-frame log noise.
    [[nodiscard]] const UiImageRegion* resolve(std::string_view key);
    [[nodiscard]] snt::core::Expected<void> set_fallback(std::string key);

    [[nodiscard]] std::shared_ptr<const UiImageAtlas> atlas() const {
        return atlas_;
    }
    [[nodiscard]] size_t image_count() const { return regions_.size(); }

private:
    [[nodiscard]] snt::core::Expected<UiImageRegion> allocate(uint32_t width,
                                                                uint32_t height);
    void write_region(const UiImageRegion& region,
                      uint32_t width,
                      uint32_t height,
                      const std::vector<uint8_t>& rgba);

    static constexpr uint32_t kPadding = 1;

    snt::assets::TextureCache texture_cache_;
    std::shared_ptr<UiImageAtlas> atlas_;
    std::unordered_map<std::string, UiImageRegion> regions_;
    std::unordered_map<std::string, std::string> source_paths_;
    std::unordered_set<std::string> missing_key_log_once_;
    std::string fallback_key_;
    uint32_t pack_x_ = kPadding;
    uint32_t pack_y_ = kPadding;
    uint32_t pack_row_height_ = 0;
};

}  // namespace snt::ui

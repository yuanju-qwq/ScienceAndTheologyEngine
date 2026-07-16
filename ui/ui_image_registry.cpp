// Dynamic UI image registry implementation.

#define SNT_LOG_CHANNEL "mui_image"
#include "ui_image_registry.h"

#include "core/log.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace snt::ui {

UiImageRegistry::UiImageRegistry()
    : atlas_(std::make_shared<UiImageAtlas>()) {
    atlas_->rgba.assign(static_cast<size_t>(atlas_->width) * atlas_->height * 4u, 0u);
}

snt::core::Expected<void> UiImageRegistry::register_file(std::string key,
                                                          std::string path) {
    if (key.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "UiImageRegistry::register_file: empty key"};
    }
    if (path.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "UiImageRegistry::register_file: empty path"};
    }
    if (const auto existing = source_paths_.find(key);
        existing != source_paths_.end() && existing->second == path) {
        return {};
    }

    auto image = texture_cache_.load_rgba(path);
    if (!image) {
        auto error = image.error();
        error.with_context("UiImageRegistry::register_file('" + key + "')");
        return error;
    }
    const std::string stable_key = key;
    auto result = register_rgba(key,
                                static_cast<uint32_t>((*image)->width),
                                static_cast<uint32_t>((*image)->height),
                                (*image)->rgba);
    if (!result) return result;
    source_paths_[stable_key] = std::move(path);
    return {};
}

snt::core::Expected<void> UiImageRegistry::register_rgba(std::string key,
                                                          uint32_t width,
                                                          uint32_t height,
                                                          std::vector<uint8_t> rgba) {
    if (key.empty() || width == 0 || height == 0) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "UiImageRegistry::register_rgba: invalid key or size"};
    }
    const size_t expected_size = static_cast<size_t>(width) * height * 4u;
    if (rgba.size() != expected_size) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "UiImageRegistry::register_rgba: payload is not RGBA"};
    }

    if (auto existing = regions_.find(key); existing != regions_.end()) {
        if (existing->second.width != width || existing->second.height != height) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "UiImageRegistry::register_rgba: existing key has a different image size"};
        }
        write_region(existing->second, width, height, rgba);
        ++atlas_->revision;
        SNT_LOG_INFO("MUI image payload updated: key='%s' size=%ux%u revision=%llu",
                     key.c_str(), width, height,
                     static_cast<unsigned long long>(atlas_->revision));
        return {};
    }

    auto region = allocate(width, height);
    if (!region) return region.error();
    write_region(*region, width, height, rgba);
    regions_.emplace(key, *region);
    ++atlas_->revision;
    SNT_LOG_INFO("MUI image registered: key='%s' size=%ux%u revision=%llu",
                 key.c_str(), width, height,
                 static_cast<unsigned long long>(atlas_->revision));
    return {};
}

const UiImageRegion* UiImageRegistry::resolve(std::string_view key) {
    if (const auto found = regions_.find(std::string(key)); found != regions_.end()) {
        return &found->second;
    }
    if (!key.empty() && missing_key_log_once_.insert(std::string(key)).second) {
        SNT_LOG_WARN("MUI image key is not registered: '%.*s'",
                     static_cast<int>(key.size()), key.data());
    }
    if (const auto fallback = regions_.find(fallback_key_); fallback != regions_.end()) {
        return &fallback->second;
    }
    return nullptr;
}

snt::core::Expected<void> UiImageRegistry::set_fallback(std::string key) {
    if (const auto found = regions_.find(key); found == regions_.end()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "UiImageRegistry::set_fallback: key is not registered"};
    }
    fallback_key_ = std::move(key);
    SNT_LOG_INFO("MUI image fallback set to '%s'", fallback_key_.c_str());
    return {};
}

snt::core::Expected<UiImageRegion> UiImageRegistry::allocate(uint32_t width,
                                                               uint32_t height) {
    const uint32_t atlas_width = atlas_->width;
    const uint32_t atlas_height = atlas_->height;
    if (width + kPadding * 2u > atlas_width ||
        height + kPadding * 2u > atlas_height) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "UiImageRegistry::allocate: image exceeds atlas dimensions"};
    }
    if (pack_x_ + width + kPadding > atlas_width) {
        pack_x_ = kPadding;
        pack_y_ += pack_row_height_ + kPadding;
        pack_row_height_ = 0;
    }
    if (pack_y_ + height + kPadding > atlas_height) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "UiImageRegistry::allocate: UI image atlas is full"};
    }

    const uint32_t x = pack_x_;
    const uint32_t y = pack_y_;
    pack_x_ += width + kPadding;
    pack_row_height_ = std::max(pack_row_height_, height);
    return UiImageRegion{
        .u0 = static_cast<float>(x) / static_cast<float>(atlas_width),
        .v0 = static_cast<float>(y) / static_cast<float>(atlas_height),
        .u1 = static_cast<float>(x + width) / static_cast<float>(atlas_width),
        .v1 = static_cast<float>(y + height) / static_cast<float>(atlas_height),
        .width = width,
        .height = height,
    };
}

void UiImageRegistry::write_region(const UiImageRegion& region,
                                   uint32_t width,
                                   uint32_t height,
                                   const std::vector<uint8_t>& rgba) {
    const uint32_t x = static_cast<uint32_t>(region.u0 * atlas_->width);
    const uint32_t y = static_cast<uint32_t>(region.v0 * atlas_->height);
    for (uint32_t row = 0; row < height; ++row) {
        uint8_t* destination = atlas_->rgba.data() +
            (static_cast<size_t>(y + row) * atlas_->width + x) * 4u;
        const uint8_t* source = rgba.data() + static_cast<size_t>(row) * width * 4u;
        std::memcpy(destination, source, static_cast<size_t>(width) * 4u);
    }
}

}  // namespace snt::ui

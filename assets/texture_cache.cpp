#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "assets/texture_cache.h"

namespace snt::assets {

snt::core::Expected<std::shared_ptr<const TextureImage>> TextureCache::load_rgba(
        const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        return it->second;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data || width <= 0 || height <= 0) {
        if (data) stbi_image_free(data);
        const char* reason = stbi_failure_reason();
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                std::string("stbi_load failed: ") + path +
                                    (reason ? std::string(" (") + reason + ")" : "")};
    }

    auto image = std::make_shared<TextureImage>();
    image->width = width;
    image->height = height;
    image->rgba.assign(data, data + static_cast<size_t>(width) * height * 4);
    stbi_image_free(data);

    cache_[path] = image;
    SNT_LOG_INFO("Texture loaded: %s (%dx%d, rgba)", path.c_str(), width, height);
    return image;
}

}  // namespace snt::assets

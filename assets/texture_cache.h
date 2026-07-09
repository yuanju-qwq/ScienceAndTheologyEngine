#pragma once

#include "core/expected.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace snt::assets {

struct TextureImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

class TextureCache {
public:
    snt::core::Expected<std::shared_ptr<const TextureImage>> load_rgba(
        const std::string& path);
    void clear() { cache_.clear(); }

private:
    std::unordered_map<std::string, std::shared_ptr<const TextureImage>> cache_;
};

}  // namespace snt::assets

#pragma once

#include "core/expected.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snt::assets {

struct JsonAsset {
    std::string text;
};

snt::core::Expected<std::vector<uint8_t>> load_binary_file(const std::string& path);
snt::core::Expected<JsonAsset> load_json_file(const std::string& path);

}  // namespace snt::assets

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/loader.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>

namespace snt::assets {

snt::core::Expected<std::vector<uint8_t>> load_binary_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return snt::core::Error{snt::core::ErrorCode::kFileOpenFailed,
                                "Failed to open binary file: " + path};
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                "Failed to query binary file size: " + path};
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!file) {
            return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                    "Failed to read binary file: " + path};
        }
    }
    return bytes;
}

snt::core::Expected<JsonAsset> load_json_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return snt::core::Error{snt::core::ErrorCode::kFileOpenFailed,
                                "Failed to open JSON file: " + path};
    }
    std::string text((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    try {
        (void)nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                std::string("JSON parse failed: ") + e.what()};
    }
    return JsonAsset{std::move(text)};
}

}  // namespace snt::assets

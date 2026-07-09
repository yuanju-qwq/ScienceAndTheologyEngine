#pragma once

#include "core/expected.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace snt::assets {

enum class ShaderStage {
    kInferFromPath,
    kVertex,
    kFragment,
    kCompute,
};

class ShaderCache {
public:
    snt::core::Expected<std::vector<uint32_t>> load_spirv_binary(const std::string& path);
    snt::core::Expected<std::vector<uint32_t>> compile_glsl_shaderc(
        const std::string& path,
        const std::string& entry_point,
        ShaderStage stage = ShaderStage::kInferFromPath);
    void clear() { spirv_cache_.clear(); }

private:
    std::unordered_map<std::string, std::vector<uint32_t>> spirv_cache_;
};

}  // namespace snt::assets

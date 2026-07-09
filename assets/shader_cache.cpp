#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/loader.h"
#include "assets/shader_cache.h"

#include <shaderc/shaderc.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace snt::assets {
namespace {

shaderc_shader_kind shaderc_kind_for(ShaderStage stage, const std::string& path) {
    if (stage == ShaderStage::kVertex) return shaderc_glsl_vertex_shader;
    if (stage == ShaderStage::kFragment) return shaderc_glsl_fragment_shader;
    if (stage == ShaderStage::kCompute) return shaderc_glsl_compute_shader;

    const std::string ext = std::filesystem::path(path).extension().string();
    if (ext == ".vert") return shaderc_glsl_vertex_shader;
    if (ext == ".frag") return shaderc_glsl_fragment_shader;
    if (ext == ".comp") return shaderc_glsl_compute_shader;
    return shaderc_glsl_infer_from_source;
}

std::string shader_cache_key(const std::string& path, const std::string& entry_point) {
    return path + "|" + entry_point;
}

}  // namespace

snt::core::Expected<std::vector<uint32_t>> ShaderCache::load_spirv_binary(
        const std::string& path) {
    auto cached = spirv_cache_.find(path);
    if (cached != spirv_cache_.end()) {
        return cached->second;
    }
    auto bytes = load_binary_file(path);
    if (!bytes) {
        return bytes.error();
    }
    if (bytes->size() % sizeof(uint32_t) != 0) {
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                "SPIR-V file size is not 4-byte aligned: " + path};
    }
    std::vector<uint32_t> words(bytes->size() / sizeof(uint32_t));
    if (!words.empty()) {
        std::memcpy(words.data(), bytes->data(), bytes->size());
    }
    spirv_cache_[path] = words;
    return words;
}

snt::core::Expected<std::vector<uint32_t>> ShaderCache::compile_glsl_shaderc(
        const std::string& path,
        const std::string& entry_point,
        ShaderStage stage) {
    const std::string cache_key = shader_cache_key(path, entry_point);
    auto cached = spirv_cache_.find(cache_key);
    if (cached != spirv_cache_.end()) {
        return cached->second;
    }

    auto source_bytes = load_binary_file(path);
    if (!source_bytes) {
        return source_bytes.error().with_context("ShaderCache::compile_glsl_shaderc");
    }
    const std::string source(
        reinterpret_cast<const char*>(source_bytes->data()),
        source_bytes->size());

    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    if (!compiler || !options) {
        if (options) shaderc_compile_options_release(options);
        if (compiler) shaderc_compiler_release(compiler);
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                "shaderc initialization failed"};
    }
    shaderc_compile_options_set_target_env(options,
                                           shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_3);
    shaderc_compile_options_set_optimization_level(
        options, shaderc_optimization_level_performance);

    const shaderc_shader_kind kind = shaderc_kind_for(stage, path);
    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        compiler,
        source.data(),
        source.size(),
        kind,
        path.c_str(),
        entry_point.c_str(),
        options);
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);

    if (!result) {
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                "shaderc_compile_into_spv returned null"};
    }
    if (shaderc_result_get_compilation_status(result) !=
        shaderc_compilation_status_success) {
        std::string message = shaderc_result_get_error_message(result);
        shaderc_result_release(result);
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                "shaderc compile failed: " + message};
    }

    const size_t bytes = shaderc_result_get_length(result);
    if (bytes % sizeof(uint32_t) != 0) {
        shaderc_result_release(result);
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                "shaderc output size is not 4-byte aligned"};
    }
    const char* output = shaderc_result_get_bytes(result);
    std::vector<uint32_t> words(bytes / sizeof(uint32_t));
    if (!words.empty()) {
        std::memcpy(words.data(), output, bytes);
    }
    shaderc_result_release(result);

    spirv_cache_[cache_key] = words;
    SNT_LOG_INFO("Shader compiled with shaderc: %s (words=%zu)",
                 path.c_str(), words.size());
    return words;
}

}  // namespace snt::assets

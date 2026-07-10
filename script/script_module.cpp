// Compiled AngelScript module wrapper — implementation.

#define SNT_LOG_CHANNEL "script"

#include "script/script_module.h"

#include <angelscript.h>
#include <scriptbuilder/scriptbuilder.h>  // CScriptBuilder (for #include)

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "core/log.h"
#include "core/error.h"
#include "core/snt_assert.h"
#include "script/script_context.h"

namespace snt::script {

ScriptModule::~ScriptModule() {
    discard();
}

ScriptModule::ScriptModule(ScriptModule&& other) noexcept
    : engine_(std::exchange(other.engine_, nullptr))
    , module_(std::exchange(other.module_, nullptr)) {
}

ScriptModule& ScriptModule::operator=(ScriptModule&& other) noexcept {
    if (this != &other) {
        discard();
        engine_ = std::exchange(other.engine_, nullptr);
        module_ = std::exchange(other.module_, nullptr);
    }
    return *this;
}

// ---- Helper: read a .as file into a string ----
static snt::core::Expected<std::string> read_file(std::string_view path) {
    std::ifstream f(path.data(), std::ios::binary);
    if (!f.is_open()) {
        return snt::core::Error{
            snt::core::ErrorCode::kFileOpenFailed,
            std::string("Failed to open script file: ") + std::string(path)
        };
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

snt::core::Expected<void> ScriptModule::compile(asIScriptEngine* engine,
                                                 std::string_view name,
                                                 std::string_view path) {
    auto src = read_file(path);
    if (!src) {
        return src.error().with_context("read_file");
    }
    return compile_source(engine, name, *src);
}

snt::core::Expected<void> ScriptModule::compile_source(asIScriptEngine* engine,
                                                        std::string_view name,
                                                        std::string_view source) {
    SNT_ASSERT_MSG(engine, "ScriptModule: null engine");
    discard();  // release any previously held module

    engine_ = engine;

    // Use CScriptBuilder to support #include directives inside scripts.
    CScriptBuilder builder;
    int r = builder.StartNewModule(engine, std::string(name).c_str());
    if (r < 0) {
        return snt::core::Error{
            snt::core::ErrorCode::kScriptCompileFailed,
            "CScriptBuilder::StartNewModule failed: " + std::to_string(r)
        };
    }

    // Add the script section. Section name is used in error messages.
    r = builder.AddSectionFromMemory("main",
                                      source.data(),
                                      static_cast<size_t>(source.size()));
    if (r < 0) {
        return snt::core::Error{
            snt::core::ErrorCode::kScriptCompileFailed,
            "CScriptBuilder::AddSectionFromMemory failed: " + std::to_string(r)
        };
    }

    r = builder.BuildModule();
    if (r < 0) {
        // This module uses a unique candidate name during transactional
        // reloads. Discard a failed candidate so diagnostics do not leave a
        // dead module registered in AngelScript's module manager.
        if (asIScriptModule* failed = engine->GetModule(
                std::string(name).c_str(), asGM_ONLY_IF_EXISTS)) {
            failed->Discard();
        }
        // Compile errors are already routed to our Logger via the
        // message callback; here we only propagate the failure code.
        return snt::core::Error{
            snt::core::ErrorCode::kScriptCompileFailed,
            "CScriptBuilder::BuildModule failed: " + std::to_string(r)
        };
    }

    module_ = engine->GetModule(std::string(name).c_str(), asGM_ONLY_IF_EXISTS);
    SNT_ASSERT_MSG(module_, "Module not found after successful build");

    SNT_LOG_DEBUG("ScriptModule '%.*s' compiled (%zu bytes)",
                  static_cast<int>(name.size()), name.data(),
                  source.size());
    return {};
}

void ScriptModule::discard() {
    if (module_) {
        module_->Discard();
        module_ = nullptr;
    }
    engine_ = nullptr;
}

asIScriptFunction* ScriptModule::get_function(std::string_view decl) const {
    if (!module_) return nullptr;
    return module_->GetFunctionByDecl(std::string(decl).c_str());
}

snt::core::Expected<void> ScriptModule::call_void(ScriptContextPool& pool,
                                                   std::string_view decl) const {
    if (!module_) {
        return snt::core::Error{
            snt::core::ErrorCode::kScriptModuleNotFound,
            "ScriptModule::call_void: module is null"
        };
    }

    asIScriptFunction* fn = get_function(decl);
    if (!fn) {
        return snt::core::Error{
            snt::core::ErrorCode::kScriptModuleNotFound,
            std::string("Function not found: ") + std::string(decl)
        };
    }

    asIScriptContext* ctx = pool.acquire();
    int r = ctx->Prepare(fn);
    if (r < 0) {
        pool.recycle(ctx);
        return snt::core::Error{
            snt::core::ErrorCode::kScriptExecuteFailed,
            "ctx->Prepare failed: " + std::to_string(r)
        };
    }

    r = ctx->Execute();
    pool.recycle(ctx);

    // asEXECUTION_FINISHED is the only success code. Aborted / exception
    // are failures; context state is already cleaned by AS.
    if (r != asEXECUTION_FINISHED) {
        return snt::core::Error{
            snt::core::ErrorCode::kScriptExecuteFailed,
            "ctx->Execute returned: " + std::to_string(r)
        };
    }
    return {};
}

}  // namespace snt::script

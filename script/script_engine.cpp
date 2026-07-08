// AngelScript engine wrapper — implementation.

#define SNT_LOG_CHANNEL "script"

#include "script/script_engine.h"

#include <angelscript.h>
#include <scriptstdstring/scriptstdstring.h>  // RegisterStdString

#include <string>

#include "core/log.h"
#include "core/error.h"     // ErrorCode
#include "core/snt_assert.h"

namespace snt::script {

ScriptEngine::ScriptEngine() = default;

ScriptEngine::~ScriptEngine() {
    if (engine_) {
        shutdown();
    }
}

snt::core::Expected<void> ScriptEngine::init() {
    engine_ = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    if (!engine_) {
        return snt::core::Error{
            snt::core::ErrorCode::kScriptEngineInitFailed,
            "asCreateScriptEngine returned null"
        };
    }

    // Route all AS messages (compile errors, warnings, runtime msgs)
    // through our Logger so they appear alongside engine logs.
    // message_callback is a static method, so use asFUNCTION (not asMETHOD).
    int r = engine_->SetMessageCallback(asFUNCTION(ScriptEngine::message_callback),
                                        nullptr,
                                        asCALL_CDECL);
    SNT_ASSERT_MSG(r >= 0, "Failed to set AS message callback");

    SNT_LOG_INFO("AngelScript engine initialized (v%s)",
                 ANGELSCRIPT_VERSION_STRING);
    return {};
}

void ScriptEngine::shutdown() {
    if (!engine_) return;
    engine_->ShutDownAndRelease();
    engine_ = nullptr;
    SNT_LOG_INFO("AngelScript engine shut down");
}

void ScriptEngine::message_callback(const asSMessageInfo* msg, void* /*param*/) {
    // AS categorizes messages by type; map them to log levels.
    switch (msg->type) {
        case asMSGTYPE_ERROR:
            SNT_LOG_ERROR("[AS] %s:%d:%d: %s",
                          msg->section, msg->row, msg->col, msg->message);
            break;
        case asMSGTYPE_WARNING:
            SNT_LOG_WARN("[AS] %s:%d:%d: %s",
                         msg->section, msg->row, msg->col, msg->message);
            break;
        default:
            SNT_LOG_INFO("[AS] %s:%d:%d: %s",
                         msg->section, msg->row, msg->col, msg->message);
            break;
    }
}

snt::core::Expected<void> ScriptEngine::register_core_types() {
    SNT_ASSERT_MSG(engine_, "ScriptEngine::init() not called");

    // string: register the standard-string add-on so scripts can use
    // `string` as a value type (with `&in` / `&out` for reference passing).
    RegisterStdString(engine_);

    // AS automatically registers int/uint/float/double/bool/int64/...
    // as primitives — no extra work needed here.
    SNT_LOG_TRACE("Registered core AS types (string + primitives)");
    return {};
}

snt::core::Expected<void> ScriptEngine::register_math_types() {
    // Phase 2 stub: register Vector3/Quaternion/Matrix + operators.
    return {};
}

snt::core::Expected<void> ScriptEngine::register_ecs_api() {
    // Phase 3 stub: register World/Entity/Transform bindings.
    return {};
}

snt::core::Expected<void> ScriptEngine::register_engine_api() {
    // Phase 3 stub: register log/input/assets bindings.
    return {};
}

}  // namespace snt::script

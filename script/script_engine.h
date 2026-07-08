// AngelScript engine wrapper.
//
// Encapsulates asIScriptEngine lifecycle: creation, message callback
// (compile/runtime diagnostics → SNT_LOG), and registration of core
// application APIs that gameplay scripts can call.
//
// Registration is split into logical groups so each subsystem can grow
// independently. Only register_core_types() has a real implementation
// right now; the rest are stubs reserved for future bindings
// (math types, ECS API, logging, input, assets).

#pragma once

#include <string>

#include "core/expected.h"  // Expected<T>

// AS types are declared as `class` in angelscript.h — see note in
// script_context.h about keeping forward-declaration tags consistent.
class asIScriptEngine;
struct asSMessageInfo;

namespace snt::script {

// Thin RAII wrapper around asIScriptEngine. Owns the engine and routes
// AS compiler/runtime messages into the engine Logger.
class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    // Create the underlying asIScriptEngine and install the message
    // callback. Returns Err on failure (rare — only fails if AS itself
    // refuses to allocate).
    snt::core::Expected<void> init();

    // Shut down and release the engine. After this call the wrapper
    // is safe to destruct, but no other method may be called.
    void shutdown();

    // Access the raw AS engine. Only valid between init() and shutdown().
    asIScriptEngine* raw() { return engine_; }

    // ---- Registration groups (stubs reserved for future bindings) ----

    // Register primitive types: int/uint/float/double/bool + string.
    // Implemented now so that basic scripts can compile and run.
    snt::core::Expected<void> register_core_types();

    // Register math types (Vector3 / Quaternion / Matrix) and their
    // operators. Stub for Phase 2.
    snt::core::Expected<void> register_math_types();

    // Register ECS API (World / Entity / Transform). Stub for Phase 3.
    snt::core::Expected<void> register_ecs_api();

    // Register engine API (log / input / assets). Stub for Phase 3.
    snt::core::Expected<void> register_engine_api();

private:
    // AS message callback: routes warnings/errors to SNT_LOG.
    static void message_callback(const asSMessageInfo* msg, void* param);

    asIScriptEngine* engine_ = nullptr;
};

}  // namespace snt::script

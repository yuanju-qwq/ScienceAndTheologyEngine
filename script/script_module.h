// Compiled AngelScript module wrapper.
//
// A module is a compiled unit of AS source code. This class wraps
// asIScriptModule to provide:
//   - compile(path) — read .as file + build module
//   - get_function(name) — find a compiled function by name
//   - call_void(name) — convenience wrapper to call a void() function
//
// The wrapper is non-owning w.r.t. asIScriptModule: modules are owned
// by the engine's module manager and discarded via Discard().

#pragma once

#include <string>
#include <string_view>

#include "core/expected.h"

// AS types are declared as `class` in angelscript.h — see note in
// script_context.h about keeping forward-declaration tags consistent.
class asIScriptModule;
class asIScriptEngine;
class asIScriptFunction;
class asIScriptContext;

namespace snt::script {

class ScriptContextPool;

class ScriptModule {
public:
    ScriptModule() = default;
    ~ScriptModule();

    ScriptModule(const ScriptModule&) = delete;
    ScriptModule& operator=(const ScriptModule&) = delete;
    ScriptModule(ScriptModule&& other) noexcept;
    ScriptModule& operator=(ScriptModule&& other) noexcept;

    // Read `path` (.as file), add it as a script section to a freshly
    // created module, and compile. On success, holds the module pointer.
    snt::core::Expected<void> compile(asIScriptEngine* engine,
                                       std::string_view name,
                                       std::string_view path);

    // Same as above but compiles inline source instead of a file.
    snt::core::Expected<void> compile_source(asIScriptEngine* engine,
                                              std::string_view name,
                                              std::string_view source);

    // Release the held module. Safe to call multiple times.
    void discard();

    // Look up a global function by its declaration, e.g.
    //   "void snt_entry()"
    // Returns null if the function is not found (no abort — caller decides).
    asIScriptFunction* get_function(std::string_view decl) const;

    // Convenience: call a void() function by declaration. The context
    // is borrowed from `pool` and returned after execution.
    snt::core::Expected<void> call_void(ScriptContextPool& pool,
                                         std::string_view decl) const;

    asIScriptModule* raw() { return module_; }
    bool valid() const { return module_ != nullptr; }

private:
    asIScriptEngine* engine_ = nullptr;
    asIScriptModule* module_  = nullptr;
};

}  // namespace snt::script

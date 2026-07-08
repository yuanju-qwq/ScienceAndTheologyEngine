// Pooled asIScriptContext wrapper.
//
// asIScriptContext is expensive to create/release (~ms scale). To avoid
// stuttering when many script functions are called per frame, we pool
// contexts: acquire() picks an idle one, recycle() returns it to the
// pool for the next call.
//
// Threading: contexts are not thread-safe. Each worker thread should
// acquire its own pool. For now we assume single-threaded main thread
// only.

#pragma once

#include <vector>

#include "core/expected.h"

// AS types are declared as `class` in angelscript.h. Forward-declare
// them with the same tag here so MSVC's name mangling stays consistent
// between headers (which see the forward decl) and .cpp files (which
// include the real header). Mismatching struct/class tags produces
// different decorated names → link errors.
class asIScriptContext;
class asIScriptEngine;

namespace snt::script {

class ScriptContextPool {
public:
    ScriptContextPool() = default;
    ~ScriptContextPool();

    ScriptContextPool(const ScriptContextPool&) = delete;
    ScriptContextPool& operator=(const ScriptContextPool&) = delete;

    // Pre-create `initial` contexts on `engine`. More contexts are
    // allocated on demand when acquire() exhausts the idle list.
    snt::core::Expected<void> init(asIScriptEngine* engine,
                                    size_t initial = 1);

    // Release all pooled contexts. After this call the pool is empty.
    void shutdown();

    // Grab an idle context. If none is idle, a new one is created on
    // demand. Never returns null.
    asIScriptContext* acquire();

    // Return a context to the pool. Pass the same pointer acquire()
    // returned — the pool does not validate ownership.
    void recycle(asIScriptContext* ctx);

    // Run one step of the AS garbage collector on the engine.
    void gc_step();

private:
    asIScriptEngine* engine_ = nullptr;
    std::vector<asIScriptContext*> idle_;
    std::vector<asIScriptContext*> all_;  // all ever created (for shutdown)
};

}  // namespace snt::script

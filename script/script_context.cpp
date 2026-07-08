// Pooled asIScriptContext wrapper — implementation.

#define SNT_LOG_CHANNEL "script"

#include "script/script_context.h"

#include <angelscript.h>

#include "core/log.h"
#include "core/error.h"
#include "core/snt_assert.h"

namespace snt::script {

ScriptContextPool::~ScriptContextPool() {
    if (!all_.empty()) {
        shutdown();
    }
}

snt::core::Expected<void> ScriptContextPool::init(asIScriptEngine* engine,
                                                    size_t initial) {
    SNT_ASSERT_MSG(engine, "ScriptContextPool::init: null engine");
    engine_ = engine;

    for (size_t i = 0; i < initial; ++i) {
        asIScriptContext* ctx = engine->CreateContext();
        if (!ctx) {
            return snt::core::Error{
                snt::core::ErrorCode::kScriptEngineInitFailed,
                "asIScriptEngine::CreateContext returned null"
            };
        }
        idle_.push_back(ctx);
        all_.push_back(ctx);
    }

    SNT_LOG_TRACE("ScriptContextPool initialized with %zu context(s)",
                  all_.size());
    return {};
}

void ScriptContextPool::shutdown() {
    for (asIScriptContext* ctx : all_) {
        if (ctx) {
            ctx->Release();
        }
    }
    idle_.clear();
    all_.clear();
    engine_ = nullptr;
}

asIScriptContext* ScriptContextPool::acquire() {
    SNT_ASSERT_MSG(engine_, "ScriptContextPool not initialized");

    if (idle_.empty()) {
        // On-demand growth: create a new context if the pool is empty.
        asIScriptContext* ctx = engine_->CreateContext();
        SNT_ASSERT_MSG(ctx, "Failed to allocate AS context on demand");
        all_.push_back(ctx);
        return ctx;
    }

    asIScriptContext* ctx = idle_.back();
    idle_.pop_back();
    return ctx;
}

void ScriptContextPool::recycle(asIScriptContext* ctx) {
    SNT_ASSERT_MSG(ctx, "recycle(null)");
    // Return the context to the idle list. It will be reused by the
    // next acquire() — the caller must not touch it after recycling.
    idle_.push_back(ctx);
}

void ScriptContextPool::gc_step() {
    if (engine_) {
        // Run a single incremental GC step (asGC_ONE_STEP). One step
        // per frame keeps memory bounded without long pauses.
        engine_->GarbageCollect(asGC_ONE_STEP);
    }
}

}  // namespace snt::script

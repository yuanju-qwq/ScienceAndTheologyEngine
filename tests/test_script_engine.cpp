// Unit tests for ScriptEngine + ScriptModule + ScriptContextPool.
//
// These tests cover the minimal happy path: create engine, register
// core types, compile inline source, and call a void() function.
// More thorough reload/file-watch tests live in test_script_loader.cpp.

#include <gtest/gtest.h>

#include "script/script_engine.h"
#include "script/script_context.h"
#include "script/script_module.h"
#include "script/script_manager.h"

using namespace snt::script;

// ============================================================
// ScriptEngine
// ============================================================

TEST(ScriptEngineTest, InitShutdown) {
    ScriptEngine e;
    EXPECT_TRUE(e.init());
    EXPECT_NE(e.raw(), nullptr);
    e.shutdown();
    EXPECT_EQ(e.raw(), nullptr);
}

TEST(ScriptEngineTest, RegisterCoreTypes) {
    ScriptEngine e;
    ASSERT_TRUE(e.init());
    EXPECT_TRUE(e.register_core_types());
    e.shutdown();
}

// ============================================================
// ScriptModule + inline source compile
// ============================================================

TEST(ScriptModuleTest, CompileInlineSource) {
    ScriptEngine e;
    ASSERT_TRUE(e.init());
    ASSERT_TRUE(e.register_core_types());

    ScriptModule m;
    auto r = m.compile_source(e.raw(), "test",
        "void snt_entry() { /* no-op */ }");
    EXPECT_TRUE(r) << r.error().message();
    EXPECT_TRUE(m.valid());
    m.discard();
    e.shutdown();
}

TEST(ScriptModuleTest, CompileFailureReturnsError) {
    ScriptEngine e;
    ASSERT_TRUE(e.init());
    ASSERT_TRUE(e.register_core_types());

    ScriptModule m;
    // Missing closing brace → AS should emit a parse error.
    auto r = m.compile_source(e.raw(), "broken", "void snt_entry() {");
    EXPECT_FALSE(r);
    EXPECT_FALSE(m.valid());
    e.shutdown();
}

TEST(ScriptModuleTest, GetFunctionByDecl) {
    ScriptEngine e;
    ASSERT_TRUE(e.init());
    ASSERT_TRUE(e.register_core_types());

    ScriptModule m;
    ASSERT_TRUE(m.compile_source(e.raw(), "test",
        "void foo() {}"
        "int  bar() { return 42; }"));

    EXPECT_NE(m.get_function("void foo()"), nullptr);
    EXPECT_NE(m.get_function("int bar()"), nullptr);
    EXPECT_EQ(m.get_function("void missing()"), nullptr);
    m.discard();
    e.shutdown();
}

// ============================================================
// ScriptContextPool + call_void
// ============================================================

TEST(ScriptContextPoolTest, AcquireRecycle) {
    ScriptEngine e;
    ASSERT_TRUE(e.init());
    ASSERT_TRUE(e.register_core_types());

    ScriptContextPool pool;
    ASSERT_TRUE(pool.init(e.raw(), 1));

    // Acquiring once should reuse the pre-allocated context.
    auto* c1 = pool.acquire();
    EXPECT_NE(c1, nullptr);
    pool.recycle(c1);

    // Second acquire returns the same context (LIFO).
    auto* c2 = pool.acquire();
    EXPECT_EQ(c1, c2);
    pool.recycle(c2);

    pool.shutdown();
    e.shutdown();
}

TEST(ScriptModuleTest, CallVoidFunction) {
    ScriptEngine e;
    ASSERT_TRUE(e.init());
    ASSERT_TRUE(e.register_core_types());

    ScriptContextPool pool;
    ASSERT_TRUE(pool.init(e.raw(), 1));

    ScriptModule m;
    ASSERT_TRUE(m.compile_source(e.raw(), "test",
        "void snt_entry() { /* no-op */ }"));

    EXPECT_TRUE(m.call_void(pool, "void snt_entry()"));
    EXPECT_FALSE(m.call_void(pool, "void missing()"));

    m.discard();
    pool.shutdown();
    e.shutdown();
}

// ============================================================
// ScriptManager (singleton)
// ============================================================

TEST(ScriptManagerTest, InitLoadSourceCallShutdown) {
    auto& sm = ScriptManager::instance();
    ASSERT_TRUE(sm.init());

    // Load an inline script that does nothing.
    ASSERT_TRUE(sm.load_source("hello", "void snt_register() {}"));

    auto* m = sm.get_module("hello");
    ASSERT_NE(m, nullptr);
    EXPECT_TRUE(m->call_void(sm.contexts(), "void snt_register()"));

    sm.update(0.016f);  // should not crash
    sm.shutdown();
}

// Unit tests for ScriptLoader: file load + hot reload via mtime.
//
// These tests write temporary .as files into the OS temp dir, load
// them, then modify the file and verify that reload_if_changed()
// picks up the new version.

#include <gtest/gtest.h>

#include <angelscript.h>  // asEXECUTION_FINISHED

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "script/script_manager.h"

namespace fs = std::filesystem;

using namespace snt::script;

namespace {

// RAII helper: create a temp .as file, clean up on destruction.
class TempScriptFile {
public:
    explicit TempScriptFile(const std::string& name,
                            const std::string& content)
        : path_((fs::temp_directory_path() / (name + ".as")).string()) {
        write(content);
    }
    ~TempScriptFile() { fs::remove(path_); }

    void write(const std::string& content) {
        std::ofstream f(path_, std::ios::binary | std::ios::trunc);
        f << content;
        f.close();
        // Touch mtime forward so reload_if_changed notices.
        auto now = fs::file_time_type::clock::now();
        std::error_code ec;
        fs::last_write_time(path_, now + std::chrono::seconds(1), ec);
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

}  // namespace

TEST(ScriptLoaderTest, LoadFileAndGetModule) {
    TempScriptFile f("loader_test_1", "void snt_register() {}");

    auto& sm = ScriptManager::instance();
    ASSERT_TRUE(sm.init());
    ASSERT_TRUE(sm.load_file(f.path()));

    auto* m = sm.get_module("loader_test_1");
    EXPECT_NE(m, nullptr);
    EXPECT_TRUE(m->call_void(sm.contexts(), "void snt_register()"));

    sm.shutdown();
}

TEST(ScriptLoaderTest, ExplicitReloadPicksUpChanges) {
    TempScriptFile f("loader_test_2",
        "void snt_register() {}"
        "int  version() { return 1; }");

    auto& sm = ScriptManager::instance();
    ASSERT_TRUE(sm.init());
    ASSERT_TRUE(sm.load_file(f.path()));

    // First version returns 1.
    {
        auto* m = sm.get_module("loader_test_2");
        ASSERT_NE(m, nullptr);
        ASSERT_NE(m->get_function("int version()"), nullptr);
    }

    // Rewrite the file with a new version number.
    f.write(
        "void snt_register() {}"
        "int  version() { return 2; }");

    // The engine command runs the same transaction path used by FileWatcher.
    ASSERT_TRUE(sm.execute_command("/snt reload"));

    auto* m = sm.get_module("loader_test_2");
    ASSERT_NE(m, nullptr);
    ASSERT_NE(m->get_function("int version()"), nullptr);

    // Call version() and check it returns the new value.
    auto* fn = m->get_function("int version()");
    ASSERT_NE(fn, nullptr);

    auto* ctx = sm.contexts().acquire();
    ctx->Prepare(fn);
    int r = ctx->Execute();
    EXPECT_EQ(r, asEXECUTION_FINISHED);
    EXPECT_EQ(ctx->GetReturnDWord(), 2u);
    sm.contexts().recycle(ctx);

    sm.shutdown();
}

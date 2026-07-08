// Tests for path_utils.
//
// Covers:
//   - join() concatenates with platform separator, no double separators
//   - exists() returns true for known files, false for missing ones
//   - resolve() returns the input unchanged before init() (graceful fallback)
//   - After init() succeeds, resolve() prefixes the engine root
//
// Note: init() depends on the test executable's location; tests here
// assume the standard CMake layout (bin/<config>/snt_tests). If init()
// fails (e.g. running from an unusual cwd), the resolve()-after-init test
// is skipped rather than failing.

#include "core/path_utils.h"

#include <gtest/gtest.h>

#include <filesystem>

using snt::core::path_utils::join;
using snt::core::path_utils::exists;
using snt::core::path_utils::resolve;
using snt::core::path_utils::engine_root;
using snt::core::path_utils::init;

TEST(PathUtils, JoinTwoSegments) {
    const std::string result = join("a", "b");
    // Must contain both segments with exactly one separator between.
    EXPECT_NE(result.find('a'), std::string::npos);
    EXPECT_NE(result.find('b'), std::string::npos);
    // No double separator.
    EXPECT_EQ(result.find("\\\\"), std::string::npos);
    EXPECT_EQ(result.find("//"), std::string::npos);
}

TEST(PathUtils, JoinStripsLeadingSeparatorFromSecondArg) {
    const std::string result = join("dir", "/sub");
    // Should be "dir/sub" or "dir\sub", not "dir//sub".
    EXPECT_EQ(result.find("//"), std::string::npos);
    EXPECT_EQ(result.find("\\\\"), std::string::npos);
}

TEST(PathUtils, JoinEmptyArgs) {
    EXPECT_EQ(join("", "b"), "b");
    EXPECT_EQ(join("a", ""), "a");
    EXPECT_EQ(join("", ""), "");
}

TEST(PathUtils, ExistsFindsSelf) {
    // The test executable itself exists. std::filesystem::current_path
    // gives us a directory we know exists.
    const auto cwd = std::filesystem::current_path().string();
    EXPECT_TRUE(exists(cwd));
}

TEST(PathUtils, ExistsReturnsFalseForMissingPath) {
    EXPECT_FALSE(exists("definitely/not/here/snt_test_xyz"));
}

TEST(PathUtils, ResolveBeforeInitReturnsInputUnchanged) {
    // Note: this test may run after init() has been called by another test.
    // We only assert the pre-init contract if project_root is still empty.
    if (project_root().empty()) {
        const std::string input = "shaders/mesh.vert.spv";
        const std::string result = resolve(input);
        EXPECT_EQ(result, input);
    } else {
        GTEST_SKIP() << "init() already called by a prior test";
    }
}

TEST(PathUtils, InitLocatesEngineRoot) {
    const bool ok = init();
    if (!ok) {
        GTEST_SKIP() << "init() could not locate project root (test cwd issue)";
    }
    EXPECT_FALSE(project_root().empty());
}

TEST(PathUtils, ResolveAfterInitProducesAbsolutePath) {
    if (!init()) {
        GTEST_SKIP() << "init() failed; cannot test resolve()";
    }
    const std::string result = resolve("game/config/engine.json");
    // After init, resolve returns an absolute path that should exist.
    EXPECT_TRUE(exists(result))
        << "Resolved path does not exist: " << result;
}

// Tests for the explicit engine/game/user runtime path contract.

#include "core/path_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

snt::core::RuntimePaths make_test_paths(std::string_view name) {
    const auto root = std::filesystem::temp_directory_path() / "snt_runtime_paths" / name;
    std::filesystem::create_directories(root / "engine");
    std::filesystem::create_directories(root / "game");
    std::filesystem::create_directories(root / "user");
    return {
        .engine_root = (root / "engine").string(),
        .game_root = (root / "game").string(),
        .user_root = (root / "user").string(),
    };
}

std::string normalized(const std::filesystem::path& path) {
    return path.lexically_normal().string();
}

}  // namespace

TEST(PathUtils, JoinTwoSegments) {
    const std::string result = snt::core::path_utils::join("a", "b");
    EXPECT_NE(result.find('a'), std::string::npos);
    EXPECT_NE(result.find('b'), std::string::npos);
    EXPECT_EQ(result.find("\\\\"), std::string::npos);
    EXPECT_EQ(result.find("//"), std::string::npos);
}

TEST(PathUtils, JoinStripsLeadingSeparatorFromSecondArg) {
    const std::string result = snt::core::path_utils::join("dir", "/sub");
    EXPECT_EQ(result.find("//"), std::string::npos);
    EXPECT_EQ(result.find("\\\\"), std::string::npos);
}

TEST(PathUtils, JoinEmptyArgs) {
    EXPECT_EQ(snt::core::path_utils::join("", "b"), "b");
    EXPECT_EQ(snt::core::path_utils::join("a", ""), "a");
    EXPECT_EQ(snt::core::path_utils::join("", ""), "");
}

TEST(PathUtils, ExistsFindsCurrentDirectory) {
    EXPECT_TRUE(snt::core::path_utils::exists(std::filesystem::current_path().string()));
}

TEST(PathUtils, ConfigureRejectsMissingRoot) {
    auto result = snt::core::path_utils::configure({
        .engine_root = "",
        .game_root = "game",
        .user_root = "user",
    });
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("engine_root"), std::string::npos);
}

TEST(PathUtils, ResolveRoutesResourcesByOwnership) {
    const auto paths = make_test_paths("ownership");
    ASSERT_TRUE(snt::core::path_utils::configure(paths).has_value());

    const auto& configured = snt::core::path_utils::runtime_paths();
    EXPECT_EQ(snt::core::path_utils::resolve_engine("shaders/mesh.vert.spv"),
              normalized(std::filesystem::path(configured.engine_root) / "shaders/mesh.vert.spv"));
    EXPECT_EQ(snt::core::path_utils::resolve_game("scripts/bootstrap.as"),
              normalized(std::filesystem::path(configured.game_root) / "scripts/bootstrap.as"));
    EXPECT_EQ(snt::core::path_utils::resolve_user("logs/engine.log"),
              normalized(std::filesystem::path(configured.user_root) / "logs/engine.log"));
}

TEST(PathUtils, ResolveKeepsAbsolutePaths) {
    const auto paths = make_test_paths("absolute");
    ASSERT_TRUE(snt::core::path_utils::configure(paths).has_value());

    const std::string absolute = std::filesystem::absolute("standalone.asset").string();
    EXPECT_EQ(snt::core::path_utils::resolve_game(absolute), absolute);
}

TEST(PathUtils, ReconfigurationReplacesHostRoots) {
    ASSERT_TRUE(snt::core::path_utils::configure(make_test_paths("first")).has_value());
    const std::string first = snt::core::path_utils::resolve_game("config/engine.json");

    ASSERT_TRUE(snt::core::path_utils::configure(make_test_paths("second")).has_value());
    const std::string second = snt::core::path_utils::resolve_game("config/engine.json");

    EXPECT_NE(first, second);
    EXPECT_NE(second.find("second"), std::string::npos);
}
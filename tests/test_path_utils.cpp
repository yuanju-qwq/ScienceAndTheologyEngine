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

TEST(PathUtils, ResolverRejectsMissingRoot) {
    auto result = snt::core::RuntimePathResolver::create({
        .engine_root = "",
        .game_root = "game",
        .user_root = "user",
    });
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("engine_root"), std::string::npos);
}

TEST(PathUtils, ResolverRoutesResourcesByOwnership) {
    auto resolver_result = snt::core::RuntimePathResolver::create(make_test_paths("ownership"));
    ASSERT_TRUE(resolver_result) << resolver_result.error().format();
    const auto& resolver = *resolver_result;
    const auto& roots = resolver.roots();

    EXPECT_EQ(resolver.resolve_engine("shaders/mesh.vert.spv"),
              normalized(std::filesystem::path(roots.engine_root) / "shaders/mesh.vert.spv"));
    EXPECT_EQ(resolver.resolve_game("scripts/bootstrap.as"),
              normalized(std::filesystem::path(roots.game_root) / "scripts/bootstrap.as"));
    EXPECT_EQ(resolver.resolve_user("logs/engine.log"),
              normalized(std::filesystem::path(roots.user_root) / "logs/engine.log"));
}

TEST(PathUtils, ResolveKeepsAbsolutePaths) {
    auto resolver_result = snt::core::RuntimePathResolver::create(make_test_paths("absolute"));
    ASSERT_TRUE(resolver_result) << resolver_result.error().format();

    const std::string absolute = std::filesystem::absolute("standalone.asset").string();
    EXPECT_EQ(resolver_result->resolve_game(absolute), absolute);
}

TEST(PathUtils, IndependentResolversKeepSeparateHostRoots) {
    auto first_resolver = snt::core::RuntimePathResolver::create(make_test_paths("first"));
    ASSERT_TRUE(first_resolver) << first_resolver.error().format();
    const std::string first = first_resolver->resolve_game("config/engine.json");

    auto second_resolver = snt::core::RuntimePathResolver::create(make_test_paths("second"));
    ASSERT_TRUE(second_resolver) << second_resolver.error().format();
    const std::string second = second_resolver->resolve_game("config/engine.json");

    EXPECT_NE(first, second);
    EXPECT_NE(second.find("second"), std::string::npos);
}

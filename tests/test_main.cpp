// GoogleTest main entry for the SNT engine test suite.
//
// Every engine test executable is a small host. It supplies explicit runtime
// paths just as a real game does, keeping tests independent of repository
// discovery logic that production code must not have.

#include "core/path_utils.h"

#include <gtest/gtest.h>

#include <filesystem>

#ifndef SNT_ENGINE_TEST_ROOT
#error "SNT_ENGINE_TEST_ROOT must be supplied by the engine test target"
#endif

class SntTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        const auto scratch = std::filesystem::temp_directory_path() / "snt_engine_tests";
        std::filesystem::create_directories(scratch / "game");
        std::filesystem::create_directories(scratch / "user");

        auto configured = snt::core::path_utils::configure({
            .engine_root = SNT_ENGINE_TEST_ROOT,
            .game_root = (scratch / "game").string(),
            .user_root = (scratch / "user").string(),
        });
        if (!configured) {
            ADD_FAILURE() << configured.error().format();
        }
    }
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new SntTestEnvironment());
    return RUN_ALL_TESTS();
}
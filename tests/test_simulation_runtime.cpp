// Integration coverage for the target-level SDL/Vulkan-free runtime path.

#include "engine/simulation_runtime.h"
#include "engine/simulation_services.h"
#include "engine/simulation_session.h"

#include "assets/asset_catalog.h"
#include "voxel/data/chunk_registry.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace {

struct SessionState {
    int register_calls = 0;
    int create_calls = 0;
    int tick_calls = 0;
    int shutdown_calls = 0;
    bool saw_content_catalog = false;
    bool saw_empty_world = false;
};

class HeadlessSession final : public snt::engine::ISimulationSession {
public:
    explicit HeadlessSession(std::shared_ptr<SessionState> state) : state_(std::move(state)) {}

    snt::core::Expected<void> register_content(
        snt::engine::SimulationServices& services) override {
        ++state_->register_calls;
        state_->saw_content_catalog = services.asset_catalog().size() == 0;
        return {};
    }

    snt::core::Expected<void> create_world(
        snt::engine::SimulationWorldSession& world) override {
        ++state_->create_calls;
        state_->saw_empty_world = world.chunks().chunk_count() == 0;
        return {};
    }

    void fixed_tick(snt::engine::FixedTickContext& context) override {
        ++state_->tick_calls;
        if (state_->tick_calls == 3) context.request_stop();
    }

    void shutdown() noexcept override { ++state_->shutdown_calls; }

private:
    std::shared_ptr<SessionState> state_;
};

std::filesystem::path make_runtime_root() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("snt_simulation_runtime_" + std::to_string(nonce));
    std::filesystem::create_directories(root / "engine");
    std::filesystem::create_directories(root / "game");
    std::filesystem::create_directories(root / "user");
    return root;
}

}  // namespace

TEST(SimulationRuntimeTest, InitializesAndTicksWithoutClientServices) {
    const auto root = make_runtime_root();
    const auto state = std::make_shared<SessionState>();

    snt::core::RuntimeConfig config;
    config.assets.manifest_path = "missing_manifest.json";
    snt::engine::SimulationRuntime runtime;
    auto init = runtime.init(config,
                             {
                                 .engine_root = (root / "engine").string(),
                                 .game_root = (root / "game").string(),
                                 .user_root = (root / "user").string(),
                             },
                             std::make_unique<HeadlessSession>(state));
    ASSERT_TRUE(init) << init.error().format();

    auto ticks = runtime.run_fixed_ticks(8);
    ASSERT_TRUE(ticks) << ticks.error().format();
    EXPECT_TRUE(runtime.stop_requested());
    EXPECT_EQ(state->register_calls, 1);
    EXPECT_EQ(state->create_calls, 1);
    EXPECT_EQ(state->tick_calls, 3);
    EXPECT_TRUE(state->saw_content_catalog);
    EXPECT_TRUE(state->saw_empty_world);

    runtime.shutdown();
    EXPECT_EQ(state->shutdown_calls, 1);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}

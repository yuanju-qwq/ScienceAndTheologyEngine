// Integration coverage for the target-level SDL/Vulkan-free runtime path.

#include "engine/simulation_runtime.h"
#include "engine/simulation_services.h"
#include "engine/simulation_session.h"
#include "engine/zig_simulation_runtime_host.h"

#include "assets/asset_catalog.h"
#include "ecs/system.h"
#include "voxel/data/chunk_registry.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

struct SessionState {
    int register_calls = 0;
    int create_calls = 0;
    int tick_calls = 0;
    int after_tick_calls = 0;
    int shutdown_calls = 0;
    bool saw_content_catalog = false;
    bool saw_empty_world = false;
    std::vector<std::string> tick_phase_order;
};

class PhaseProbeSystem final : public snt::ecs::System {
public:
    explicit PhaseProbeSystem(std::shared_ptr<SessionState> state) : state_(std::move(state)) {}

    snt::ecs::SystemMetadata metadata() const override {
        return {.name = "simulation_runtime_phase_probe",
                .affinity = snt::ecs::SystemThreadAffinity::MainThread};
    }

    void update(snt::ecs::World&, float) override { state_->tick_phase_order.push_back("scheduler"); }

private:
    std::shared_ptr<SessionState> state_;
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
        auto registered = world.register_main_system(std::make_shared<PhaseProbeSystem>(state_));
        if (!registered) return registered.error();
        return {};
    }

    snt::core::Expected<void> fixed_tick(snt::engine::FixedTickContext& context) override {
        ++state_->tick_calls;
        state_->tick_phase_order.push_back("session");
        if (state_->tick_calls == 3) context.request_stop();
        return {};
    }

    snt::core::Expected<void> after_fixed_tick(snt::engine::FixedTickContext&) override {
        ++state_->after_tick_calls;
        state_->tick_phase_order.push_back("post");
        return {};
    }

    void shutdown() noexcept override { ++state_->shutdown_calls; }

private:
    std::shared_ptr<SessionState> state_;
};

struct FailingSessionState {
    bool fail_registration = false;
    int register_calls = 0;
    int create_calls = 0;
    int tick_calls = 0;
    int after_tick_calls = 0;
    int shutdown_calls = 0;
};

class FailingSession final : public snt::engine::ISimulationSession {
public:
    explicit FailingSession(std::shared_ptr<FailingSessionState> state) : state_(std::move(state)) {}

    snt::core::Expected<void> register_content(snt::engine::SimulationServices&) override {
        ++state_->register_calls;
        if (state_->fail_registration) {
            return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                    "intentional Zig adapter registration failure"};
        }
        return {};
    }

    snt::core::Expected<void> create_world(snt::engine::SimulationWorldSession&) override {
        ++state_->create_calls;
        return {};
    }

    snt::core::Expected<void> fixed_tick(snt::engine::FixedTickContext&) override {
        ++state_->tick_calls;
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "intentional Zig adapter fixed-tick failure"};
    }

    snt::core::Expected<void> after_fixed_tick(snt::engine::FixedTickContext&) override {
        ++state_->after_tick_calls;
        return {};
    }

    void shutdown() noexcept override { ++state_->shutdown_calls; }

private:
    std::shared_ptr<FailingSessionState> state_;
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
    EXPECT_EQ(state->after_tick_calls, 3);
    EXPECT_EQ(state->tick_phase_order,
              (std::vector<std::string>{"session", "scheduler", "post",
                                        "session", "scheduler", "post",
                                        "session", "scheduler", "post"}));
    EXPECT_TRUE(state->saw_content_catalog);
    EXPECT_TRUE(state->saw_empty_world);

    runtime.shutdown();
    EXPECT_EQ(state->shutdown_calls, 1);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(ZigSimulationRuntimeHostTest, DrivesExistingSessionThroughZigLifecycle) {
    const auto root = make_runtime_root();
    const auto state = std::make_shared<SessionState>();

    snt::core::RuntimeConfig config;
    config.assets.manifest_path = "missing_manifest.json";
    auto host = snt::engine::ZigSimulationRuntimeHost::create(
        config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<HeadlessSession>(state));
    ASSERT_TRUE(host) << host.error().format();

    auto ticks = (*host)->run_fixed_ticks(8);
    ASSERT_TRUE(ticks) << ticks.error().format();
    EXPECT_TRUE((*host)->stop_requested());
    EXPECT_EQ((*host)->last_error(), nullptr);
    EXPECT_EQ(state->register_calls, 1);
    EXPECT_EQ(state->create_calls, 1);
    EXPECT_EQ(state->tick_calls, 3);
    EXPECT_EQ(state->after_tick_calls, 3);
    EXPECT_EQ(state->tick_phase_order,
              (std::vector<std::string>{"session", "scheduler", "post",
                                        "session", "scheduler", "post",
                                        "session", "scheduler", "post"}));
    EXPECT_TRUE(state->saw_content_catalog);
    EXPECT_TRUE(state->saw_empty_world);

    (*host)->shutdown();
    EXPECT_EQ(state->shutdown_calls, 1);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(ZigSimulationRuntimeHostTest, StopsAndPreservesCppFailureFromSessionCallback) {
    const auto root = make_runtime_root();
    const auto state = std::make_shared<FailingSessionState>();

    snt::core::RuntimeConfig config;
    config.assets.manifest_path = "missing_manifest.json";
    auto host = snt::engine::ZigSimulationRuntimeHost::create(
        config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<FailingSession>(state));
    ASSERT_TRUE(host) << host.error().format();

    auto ticks = (*host)->run_fixed_ticks(1);
    ASSERT_FALSE(ticks);
    const auto* callback_error = (*host)->last_error();
    ASSERT_NE(callback_error, nullptr);
    EXPECT_EQ(callback_error->message(), "intentional Zig adapter fixed-tick failure");
    EXPECT_TRUE((*host)->stop_requested());
    EXPECT_EQ(state->register_calls, 1);
    EXPECT_EQ(state->create_calls, 1);
    EXPECT_EQ(state->tick_calls, 1);
    EXPECT_EQ(state->after_tick_calls, 0);

    (*host)->shutdown();
    EXPECT_EQ(state->shutdown_calls, 1);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(ZigSimulationRuntimeHostTest, CleansUpSessionWhenInitializeCallbackFails) {
    const auto root = make_runtime_root();
    const auto state = std::make_shared<FailingSessionState>();
    state->fail_registration = true;

    snt::core::RuntimeConfig config;
    config.assets.manifest_path = "missing_manifest.json";
    auto host = snt::engine::ZigSimulationRuntimeHost::create(
        config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<FailingSession>(state));
    ASSERT_FALSE(host);
    EXPECT_EQ(host.error().message(), "intentional Zig adapter registration failure");
    EXPECT_EQ(state->register_calls, 1);
    EXPECT_EQ(state->create_calls, 0);
    EXPECT_EQ(state->tick_calls, 0);
    EXPECT_EQ(state->after_tick_calls, 0);
    EXPECT_EQ(state->shutdown_calls, 1);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}

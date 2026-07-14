// SimulationRuntime implementation.

#define SNT_LOG_CHANNEL "simulation_runtime"
#include "engine/simulation_runtime.h"

#include "engine/simulation_services.h"
#include "engine/simulation_session.h"

#include "assets/asset_catalog.h"
#include "assets/filesystem_asset_source.h"
#include "core/events.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/memory_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/system_scheduler.h"
#include "ecs/world.h"
#include "script/script_manager.h"
#include "voxel/data/chunk_registry.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace snt::engine {
namespace {

struct TickStats {
    static constexpr float kTickRate = 20.0f;
    static constexpr float kTickMs = 1000.0f / kTickRate;
    static constexpr int kMaxCatchup = 5;

    float accumulator = 0.0f;
    float last_tick_ms = 0.0f;
    int ticks_this_second = 0;
    float second_timer = 0.0f;
    float tps = 0.0f;
    uint32_t dropped_debt_this_second = 0;

    template <typename TickFn>
    void consume(float frame_ms, TickFn tick_fn) {
        accumulator += frame_ms;
        second_timer += frame_ms;

        int ticks_run = 0;
        while (accumulator >= kTickMs && ticks_run < kMaxCatchup) {
            const auto started = std::chrono::high_resolution_clock::now();
            tick_fn();
            const auto finished = std::chrono::high_resolution_clock::now();
            last_tick_ms = static_cast<float>(
                std::chrono::duration<double, std::milli>(finished - started).count());
            accumulator -= kTickMs;
            ++ticks_this_second;
            ++ticks_run;
        }

        if (ticks_run >= kMaxCatchup && accumulator >= kTickMs) {
            accumulator = 0.0f;
            ++dropped_debt_this_second;
        }

        if (second_timer >= 1000.0f) {
            tps = static_cast<float>(ticks_this_second);
            if (dropped_debt_this_second > 0) {
                SNT_LOG_WARN("Fixed-tick debt dropped %u time(s); last tick %.2f ms",
                             dropped_debt_this_second, last_tick_ms);
            }
            ticks_this_second = 0;
            dropped_debt_this_second = 0;
            second_timer -= 1000.0f;
        }
    }
};

const SimulationStats kStoppedStats{};

}  // namespace

struct SimulationRuntime::Impl {
    snt::core::RuntimeConfig config;
    std::optional<snt::core::RuntimePathResolver> paths;
    snt::core::RealClock real_clock;
    snt::core::IClock* clock = &real_clock;

    snt::core::JobSystemP2 job_system;
    std::unique_ptr<snt::ecs::SystemScheduler> system_scheduler;
    snt::ecs::World world;
    snt::ecs::EventBus event_bus;
    snt::voxel::ChunkRegistry chunk_registry;
    snt::script::ScriptManager script_manager;
    std::optional<snt::assets::FilesystemAssetSource> content_source;
    std::optional<snt::assets::AssetCatalog> asset_catalog;

    std::unique_ptr<SimulationServices> services;
    std::unique_ptr<SimulationWorldSession> world_session;
    std::unique_ptr<ISimulationSession> session;
    TickStats tick_stats;
    SimulationStats stats;
    uint64_t tick_index = 0;
    std::atomic_bool stop_requested = false;
    bool session_started = false;
    bool file_log_sink_open = false;
};

SimulationServices::SimulationServices(const snt::core::RuntimeConfig& config,
                                       const snt::core::RuntimePathResolver& paths,
                                       snt::core::IClock& clock,
                                       snt::core::Logger& logger,
                                       snt::core::JobSystem& jobs,
                                       snt::assets::IAssetSource& content_source,
                                       const snt::assets::AssetCatalog& asset_catalog,
                                       snt::script::ScriptManager& scripts)
    : config_(&config), paths_(&paths), clock_(&clock), logger_(&logger), jobs_(&jobs),
      content_source_(&content_source), asset_catalog_(&asset_catalog), scripts_(&scripts) {}

const snt::core::RuntimeConfig& SimulationServices::config() const noexcept { return *config_; }
const snt::core::RuntimePathResolver& SimulationServices::paths() const noexcept {
    return *paths_;
}
snt::core::IClock& SimulationServices::clock() const noexcept { return *clock_; }
snt::core::Logger& SimulationServices::logger() const noexcept { return *logger_; }
snt::core::JobSystem& SimulationServices::jobs() const noexcept { return *jobs_; }
snt::assets::IAssetSource& SimulationServices::content_source() const noexcept {
    return *content_source_;
}
const snt::assets::AssetCatalog& SimulationServices::asset_catalog() const noexcept {
    return *asset_catalog_;
}
snt::script::ScriptManager& SimulationServices::scripts() const noexcept { return *scripts_; }

snt::ecs::World& SimulationWorldSession::world() const noexcept {
    return runtime_->impl_->world;
}
snt::voxel::ChunkRegistry& SimulationWorldSession::chunks() const noexcept {
    return runtime_->impl_->chunk_registry;
}
snt::ecs::EventBus& SimulationWorldSession::events() const noexcept {
    return runtime_->impl_->event_bus;
}

snt::core::Expected<snt::ecs::SystemHandle> SimulationWorldSession::register_main_system(
    std::shared_ptr<snt::ecs::System> system) {
    if (!runtime_ || !runtime_->impl_ || !runtime_->impl_->system_scheduler) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "SimulationWorldSession scheduler is unavailable"};
    }
    return runtime_->impl_->system_scheduler->register_main(std::move(system));
}

snt::core::Expected<snt::ecs::SystemHandle> SimulationWorldSession::register_worker_system(
    std::shared_ptr<snt::ecs::IWorkerSystem> system) {
    if (!runtime_ || !runtime_->impl_ || !runtime_->impl_->system_scheduler) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "SimulationWorldSession scheduler is unavailable"};
    }
    return runtime_->impl_->system_scheduler->register_worker(std::move(system));
}

snt::core::Expected<void> SimulationWorldSession::set_system_enabled(
    snt::ecs::SystemHandle handle, bool enabled) {
    if (!runtime_ || !runtime_->impl_ || !runtime_->impl_->system_scheduler) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "SimulationWorldSession scheduler is unavailable"};
    }
    return runtime_->impl_->system_scheduler->set_enabled(handle, enabled);
}

void FixedTickContext::request_stop() const noexcept {
    if (runtime_) runtime_->request_stop();
}

SimulationRuntime::SimulationRuntime() : impl_(std::make_unique<Impl>()) {}
SimulationRuntime::~SimulationRuntime() { shutdown(); }

snt::core::Expected<void> SimulationRuntime::init(
    const snt::core::RuntimeConfig& config,
    snt::core::RuntimePaths runtime_paths,
    std::unique_ptr<ISimulationSession> session) {
    if (!session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "SimulationRuntime::init requires an ISimulationSession"};
    }
    if (auto result = init_services(config, std::move(runtime_paths)); !result) {
        return result.error();
    }
    return attach_session(std::move(session));
}

snt::core::Expected<void> SimulationRuntime::init_services(
    const snt::core::RuntimeConfig& config,
    snt::core::RuntimePaths runtime_paths) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->services || impl_->paths) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "SimulationRuntime::init_services called twice"};
    }

    SNT_LOG_INFO("Starting SNT simulation runtime");
    impl_->config = config;
    impl_->stop_requested.store(false);

    auto resolved_paths = snt::core::RuntimePathResolver::create(std::move(runtime_paths));
    if (!resolved_paths) {
        auto error = resolved_paths.error();
        error.with_context("SimulationRuntime::init(RuntimePaths)");
        return error;
    }
    impl_->paths.emplace(std::move(*resolved_paths));

    impl_->job_system.init();
    SNT_LOG_INFO("Simulation job system started: %d workers", impl_->job_system.worker_count());
    impl_->system_scheduler = std::make_unique<snt::ecs::SystemScheduler>(impl_->job_system);

    const std::string log_path = impl_->paths->resolve_user("logs/engine.log");
    try {
        std::filesystem::create_directories(std::filesystem::path(log_path).parent_path());
    } catch (const std::exception& error) {
        SNT_LOG_WARN("Failed to create log directory: %s", error.what());
    }
    impl_->file_log_sink_open = snt::core::Logger::instance().add_file_sink(log_path.c_str());
    if (!impl_->file_log_sink_open) {
        SNT_LOG_WARN("Failed to open log file '%s'; logging to stderr only", log_path.c_str());
    } else {
        SNT_LOG_INFO("Simulation logging to file: %s", log_path.c_str());
    }

    auto content_source = snt::assets::FilesystemAssetSource::create(
        std::filesystem::path(impl_->paths->roots().game_root));
    if (!content_source) {
        auto error = content_source.error();
        error.with_context("SimulationRuntime::init(FilesystemAssetSource)");
        return error;
    }
    impl_->content_source.emplace(std::move(*content_source));

    auto asset_catalog = snt::assets::AssetCatalog::load(
        *impl_->content_source,
        snt::assets::AssetSourceRequest{.requested_path = config.assets.manifest_path});
    if (!asset_catalog) {
        auto error = asset_catalog.error();
        error.with_context("SimulationRuntime::init(AssetCatalog)");
        return error;
    }
    impl_->asset_catalog.emplace(std::move(*asset_catalog));

    impl_->services = std::unique_ptr<SimulationServices>(new SimulationServices(
        impl_->config, *impl_->paths, *impl_->clock, snt::core::Logger::instance(),
        impl_->job_system, *impl_->content_source, *impl_->asset_catalog,
        impl_->script_manager));
    impl_->world_session = std::unique_ptr<SimulationWorldSession>(
        new SimulationWorldSession(*this));

    SNT_LOG_INFO("Simulation services initialized without SDL or Vulkan");
    return {};
}

snt::core::Expected<void> SimulationRuntime::attach_session(
    std::unique_ptr<ISimulationSession> session) {
    if (!impl_ || !impl_->services || !impl_->world_session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "SimulationRuntime services are unavailable"};
    }
    if (!session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "SimulationRuntime::attach_session requires a session"};
    }
    if (impl_->session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "SimulationRuntime already owns a session"};
    }

    impl_->session = std::move(session);
    impl_->session_started = true;
    if (auto result = impl_->session->register_content(*impl_->services); !result) {
        auto error = result.error();
        error.with_context("SimulationRuntime::attach_session(register_content)");
        return error;
    }
    if (auto result = impl_->session->create_world(*impl_->world_session); !result) {
        auto error = result.error();
        error.with_context("SimulationRuntime::attach_session(create_world)");
        return error;
    }

    SNT_LOG_INFO("Simulation session initialized");
    return {};
}

snt::core::Expected<void> SimulationRuntime::run_one_fixed_tick() {
    if (!impl_ || !impl_->session || !impl_->services || !impl_->world_session ||
        !impl_->system_scheduler) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "SimulationRuntime fixed tick is unavailable"};
    }

    constexpr float kFixedDeltaSeconds = TickStats::kTickMs / 1000.0f;
    FixedTickContext context(*this, *impl_->services, *impl_->world_session,
                             kFixedDeltaSeconds, ++impl_->tick_index);
    if (auto result = impl_->session->fixed_tick(context); !result) {
        auto error = result.error();
        error.with_context("SimulationRuntime::run_one_fixed_tick(session pre-tick)");
        return error;
    }
    if (auto result = impl_->system_scheduler->fixed_tick(impl_->world, kFixedDeltaSeconds);
        !result) {
        auto error = result.error();
        error.with_context("SimulationRuntime::run_one_fixed_tick(SystemScheduler)");
        return error;
    }
    if (auto result = impl_->session->after_fixed_tick(context); !result) {
        auto error = result.error();
        error.with_context("SimulationRuntime::run_one_fixed_tick(session post-tick)");
        return error;
    }
    return {};
}

snt::core::Expected<void> SimulationRuntime::run_fixed_ticks(uint64_t tick_count) {
    if (!impl_ || !impl_->session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "SimulationRuntime::run_fixed_ticks requires an initialized session"};
    }

    for (uint64_t index = 0; index < tick_count && !stop_requested(); ++index) {
        const auto started = std::chrono::high_resolution_clock::now();
        if (auto result = run_one_fixed_tick(); !result) {
            return result.error();
        }
        const auto finished = std::chrono::high_resolution_clock::now();
        impl_->tick_stats.last_tick_ms = static_cast<float>(
            std::chrono::duration<double, std::milli>(finished - started).count());
    }
    impl_->stats = {
        .tps = impl_->tick_stats.tps,
        .mspt = impl_->tick_stats.last_tick_ms,
        .job_workers = impl_->job_system.worker_count(),
    };
    return {};
}

snt::core::Expected<void> SimulationRuntime::advance_time(snt::core::DurationMs elapsed) {
    if (!impl_ || !impl_->session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "SimulationRuntime::advance_time requires an initialized session"};
    }
    if (elapsed < snt::core::DurationMs::zero()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "SimulationRuntime::advance_time rejects negative elapsed time"};
    }

    bool failed = false;
    snt::core::Error failure;
    impl_->tick_stats.consume(elapsed.count(), [&] {
        if (failed || stop_requested()) return;
        if (auto result = run_one_fixed_tick(); !result) {
            failure = result.error();
            failed = true;
        }
    });
    impl_->stats = {
        .tps = impl_->tick_stats.tps,
        .mspt = impl_->tick_stats.last_tick_ms,
        .job_workers = impl_->job_system.worker_count(),
    };
    if (failed) return failure;
    return {};
}

void SimulationRuntime::run() {
    if (!impl_ || !impl_->session || !impl_->services) return;

    auto last_time = impl_->clock->now();
    while (!stop_requested()) {
        const auto now = impl_->clock->now();
        const auto elapsed = impl_->clock->delta_since(last_time);
        last_time = now;
        if (auto result = advance_time(elapsed); !result) {
            SNT_LOG_ERROR("Simulation fixed-tick scheduler failed; ending runtime loop: %s",
                          result.error().format().c_str());
            request_stop();
            break;
        }

        // A server has no window-event wait. Sleep briefly while its fixed
        // accumulator is below the next 20 TPS boundary without emitting any
        // high-frequency diagnostics.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SimulationRuntime::request_stop() noexcept {
    if (impl_) impl_->stop_requested.store(true);
}

bool SimulationRuntime::stop_requested() const noexcept {
    return impl_ && impl_->stop_requested.load();
}

const SimulationStats& SimulationRuntime::stats() const noexcept {
    return impl_ ? impl_->stats : kStoppedStats;
}

SimulationServices& SimulationRuntime::services() noexcept { return *impl_->services; }
SimulationWorldSession& SimulationRuntime::world_session() noexcept {
    return *impl_->world_session;
}
snt::core::IClock& SimulationRuntime::clock() { return *impl_->clock; }

void SimulationRuntime::set_clock(snt::core::IClock* clock) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->clock = clock != nullptr ? clock : &impl_->real_clock;
}

void SimulationRuntime::shutdown_session() noexcept {
    if (!impl_) return;
    if (impl_->session_started && impl_->session) {
        impl_->session->shutdown();
        impl_->session_started = false;
    }
    impl_->session.reset();
}

void SimulationRuntime::shutdown_execution() noexcept {
    if (!impl_) return;

    // Event sinks can borrow scheduler-owned systems. Disconnect them before
    // releasing systems, then join every worker before any client resource is
    // allowed to disappear.
    impl_->event_bus.clear();
    if (impl_->system_scheduler) {
        impl_->system_scheduler->shutdown();
        impl_->system_scheduler.reset();
    }
    impl_->job_system.shutdown();
}

void SimulationRuntime::shutdown_services() noexcept {
    if (!impl_) return;

    impl_->script_manager.shutdown();
    impl_->world_session.reset();
    impl_->services.reset();
    impl_->asset_catalog.reset();
    impl_->content_source.reset();

    SNT_LOG_INFO("Simulation runtime shutdown complete");
    if (impl_->file_log_sink_open) {
        snt::core::Logger::instance().remove_file_sink();
        impl_->file_log_sink_open = false;
    }
    impl_.reset();
    snt::core::MemoryTracker::instance().log_stats();
}

void SimulationRuntime::shutdown() {
    if (!impl_) return;
    request_stop();
    shutdown_session();
    shutdown_execution();
    shutdown_services();
}

}  // namespace snt::engine

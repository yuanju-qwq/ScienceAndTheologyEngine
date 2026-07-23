// C++ ISimulationSession adapter driven by the Zig deterministic host.
//
// This headless bridge keeps the existing C++ service graph and session
// objects on the C++ side. Zig owns only the C ABI lifecycle, tick ordering,
// command queue, and snapshot leases; no C++ object crosses that boundary.

#pragma once

#include "abi/runtime_host_abi.h"
#include "core/error.h"
#include "core/expected.h"
#include "core/path_utils.h"
#include "core/runtime_config.h"

#include <cstdint>
#include <memory>

namespace snt::engine {

class ISimulationSession;
class SimulationRuntime;
struct SimulationStats;

// Owns one SDL/Vulkan-free SimulationRuntime through the Zig host contract.
// The caller keeps any SntRuntimeHostCallbacks user_data valid until shutdown
// returns. RuntimeConfig stays C++-owned in this first adapter slice; the ABI
// configuration blobs are intentionally absent until their schema is frozen.
class ZigSimulationRuntimeHost {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<ZigSimulationRuntimeHost>> create(
        snt::core::RuntimeConfig config,
        snt::core::RuntimePaths runtime_paths,
        std::unique_ptr<ISimulationSession> session,
        SntRuntimeHostCallbacks host_callbacks = SNT_RUNTIME_HOST_CALLBACKS_INIT);

    ~ZigSimulationRuntimeHost();

    ZigSimulationRuntimeHost(const ZigSimulationRuntimeHost&) = delete;
    ZigSimulationRuntimeHost& operator=(const ZigSimulationRuntimeHost&) = delete;
    ZigSimulationRuntimeHost(ZigSimulationRuntimeHost&&) = delete;
    ZigSimulationRuntimeHost& operator=(ZigSimulationRuntimeHost&&) = delete;

    // Runs at most tick_count complete deterministic ticks. A session calling
    // FixedTickContext::request_stop() completes its current tick and stops
    // subsequent calls, matching SimulationRuntime's existing semantics.
    [[nodiscard]] snt::core::Expected<void> run_fixed_ticks(uint64_t tick_count);
    void request_stop() noexcept;
    [[nodiscard]] bool stop_requested() const noexcept;
    [[nodiscard]] const SimulationStats& stats() const noexcept;
    [[nodiscard]] const snt::core::Error* last_error() const noexcept;
    void shutdown() noexcept;

private:
    ZigSimulationRuntimeHost(snt::core::RuntimeConfig config,
                             snt::core::RuntimePaths runtime_paths,
                             std::unique_ptr<ISimulationSession> session,
                             SntRuntimeHostCallbacks host_callbacks);

    [[nodiscard]] snt::core::Expected<void> initialize_host();
    [[nodiscard]] snt::core::Expected<void> status_error(SntAbiStatus status,
                                                          const char* operation);
    void record_failure(const snt::core::Error& error, const char* operation);
    [[nodiscard]] SntAbiStatus run_initialize(
        const SntRuntimeSessionInitializeContext* context);
    [[nodiscard]] SntAbiStatus run_before_fixed_tick(
        SntRuntimeHost* host,
        const SntRuntimeFixedTickContext* context);
    [[nodiscard]] SntAbiStatus run_fixed_systems(
        SntRuntimeHost* host,
        const SntRuntimeFixedTickContext* context);
    [[nodiscard]] SntAbiStatus run_after_fixed_tick(
        SntRuntimeHost* host,
        const SntRuntimeFixedTickContext* context);
    [[nodiscard]] SntAbiStatus propagate_stop_request(SntRuntimeHost* host,
                                                       const char* operation);
    [[nodiscard]] SntAbiStatus unexpected_callback_exception(const char* operation) noexcept;
    void run_shutdown() noexcept;

    static SntAbiStatus on_initialize(void* user_data,
                                      const SntRuntimeSessionInitializeContext* context) noexcept;
    static SntAbiStatus on_before_fixed_tick(void* user_data,
                                              SntRuntimeHost* host,
                                              const SntRuntimeFixedTickContext* context) noexcept;
    static SntAbiStatus on_fixed_systems(void* user_data,
                                         SntRuntimeHost* host,
                                         const SntRuntimeFixedTickContext* context) noexcept;
    static SntAbiStatus on_after_fixed_tick(void* user_data,
                                            SntRuntimeHost* host,
                                            const SntRuntimeFixedTickContext* context) noexcept;
    static void on_shutdown(void* user_data, SntRuntimeHost* host) noexcept;

    snt::core::RuntimeConfig config_;
    snt::core::RuntimePaths runtime_paths_;
    std::unique_ptr<ISimulationSession> pending_session_;
    SntRuntimeHostCallbacks host_callbacks_ = SNT_RUNTIME_HOST_CALLBACKS_INIT;
    std::unique_ptr<SimulationRuntime> runtime_;
    SntRuntimeHost* host_ = nullptr;
    snt::core::Error last_error_;
    bool has_last_error_ = false;
};

}  // namespace snt::engine

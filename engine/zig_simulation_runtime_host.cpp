// C++ ISimulationSession adapter driven by the Zig deterministic host.

#define SNT_LOG_CHANNEL "zig_simulation_runtime_host"
#include "engine/zig_simulation_runtime_host.h"

#include "engine/simulation_runtime.h"
#include "engine/simulation_session.h"

#include "core/log.h"

#include <limits>
#include <string>
#include <utility>

namespace snt::engine {
namespace {

SntAbiByteView byte_view(const std::string& value) {
    return {reinterpret_cast<const uint8_t*>(value.data()),
            static_cast<uint64_t>(value.size())};
}

bool copy_byte_view(SntAbiByteView view, std::string* out_value) {
    if (out_value == nullptr || (view.data == nullptr && view.size_bytes != 0u) ||
        view.size_bytes > static_cast<uint64_t>(out_value->max_size())) {
        return false;
    }

    if (view.size_bytes == 0u) {
        out_value->clear();
        return true;
    }

    out_value->assign(reinterpret_cast<const char*>(view.data),
                      static_cast<size_t>(view.size_bytes));
    return true;
}

snt::core::ErrorCode error_code_for_status(SntAbiStatus status) {
    switch (status) {
    case SNT_ABI_STATUS_INVALID_ARGUMENT:
    case SNT_ABI_STATUS_INCOMPATIBLE_VERSION:
        return snt::core::ErrorCode::kInvalidArgument;
    case SNT_ABI_STATUS_NOT_READY:
    case SNT_ABI_STATUS_INVALID_STATE:
        return snt::core::ErrorCode::kInvalidState;
    default:
        return snt::core::ErrorCode::kUnknown;
    }
}

SntAbiStatus status_for_error(const snt::core::Error& error) {
    switch (error.code()) {
    case snt::core::ErrorCode::kInvalidArgument:
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    case snt::core::ErrorCode::kInvalidState:
    case snt::core::ErrorCode::kCancelled:
        return SNT_ABI_STATUS_INVALID_STATE;
    default:
        return SNT_ABI_STATUS_INTERNAL_ERROR;
    }
}

bool valid_tick_context(const SntRuntimeFixedTickContext* context) {
    return context != nullptr &&
           context->struct_size == sizeof(SntRuntimeFixedTickContext) &&
           context->reserved == 0u && context->simulation_tick != 0u &&
           context->fixed_tick_period_nanoseconds ==
               kSimulationFixedTickPeriodNanoseconds;
}

}  // namespace

snt::core::Expected<std::unique_ptr<ZigSimulationRuntimeHost>>
ZigSimulationRuntimeHost::create(snt::core::RuntimeConfig config,
                                 snt::core::RuntimePaths runtime_paths,
                                 std::unique_ptr<ISimulationSession> session,
                                 SntRuntimeHostCallbacks host_callbacks) {
    if (!session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "ZigSimulationRuntimeHost requires an ISimulationSession"};
    }

    auto host = std::unique_ptr<ZigSimulationRuntimeHost>(new ZigSimulationRuntimeHost(
        std::move(config), std::move(runtime_paths), std::move(session), host_callbacks));
    if (auto result = host->initialize_host(); !result) {
        auto error = result.error();
        host->shutdown();
        return error;
    }
    return host;
}

ZigSimulationRuntimeHost::ZigSimulationRuntimeHost(
    snt::core::RuntimeConfig config,
    snt::core::RuntimePaths runtime_paths,
    std::unique_ptr<ISimulationSession> session,
    SntRuntimeHostCallbacks host_callbacks)
    : config_(std::move(config)), runtime_paths_(std::move(runtime_paths)),
      pending_session_(std::move(session)), host_callbacks_(host_callbacks),
      runtime_(std::make_unique<SimulationRuntime>()) {}

ZigSimulationRuntimeHost::~ZigSimulationRuntimeHost() { shutdown(); }

snt::core::Expected<void> ZigSimulationRuntimeHost::initialize_host() {
    if (host_ != nullptr || !runtime_ || !pending_session_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ZigSimulationRuntimeHost cannot be initialized twice"};
    }

    SntRuntimeHostCreateInfo create_info = SNT_RUNTIME_HOST_CREATE_INFO_INIT;
    create_info.fixed_tick_period_nanoseconds = kSimulationFixedTickPeriodNanoseconds;
    create_info.paths.engine_root_utf8 = byte_view(runtime_paths_.engine_root);
    create_info.paths.game_root_utf8 = byte_view(runtime_paths_.game_root);
    create_info.paths.user_root_utf8 = byte_view(runtime_paths_.user_root);
    create_info.host_callbacks = host_callbacks_;
    create_info.session_callbacks.user_data = this;
    create_info.session_callbacks.initialize = on_initialize;
    create_info.session_callbacks.before_fixed_tick = on_before_fixed_tick;
    create_info.session_callbacks.run_fixed_systems = on_fixed_systems;
    create_info.session_callbacks.after_fixed_tick = on_after_fixed_tick;
    create_info.session_callbacks.shutdown = on_shutdown;

    SntRuntimeHost* created_host = nullptr;
    const SntAbiStatus status = snt_runtime_host_create(&create_info, &created_host);
    if (status != SNT_ABI_STATUS_OK) {
        if (!has_last_error_) {
            return status_error(status, "ZigSimulationRuntimeHost::create");
        }
        return last_error_;
    }
    if (created_host == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "ZigSimulationRuntimeHost create returned no host"};
    }

    host_ = created_host;
    SNT_LOG_INFO("C++ simulation session attached to Zig deterministic host");
    return {};
}

snt::core::Expected<void> ZigSimulationRuntimeHost::status_error(SntAbiStatus status,
                                                                  const char* operation) {
    snt::core::Error error{error_code_for_status(status), snt_abi_status_message(status)};
    record_failure(error, operation);
    return last_error_;
}

void ZigSimulationRuntimeHost::record_failure(const snt::core::Error& error,
                                               const char* operation) {
    last_error_ = error;
    last_error_.with_context(operation);
    has_last_error_ = true;
    SNT_LOG_ERROR("%s", last_error_.format().c_str());
}

SntAbiStatus ZigSimulationRuntimeHost::run_initialize(
    const SntRuntimeSessionInitializeContext* context) {
    if (context == nullptr || context->struct_size != sizeof(SntRuntimeSessionInitializeContext) ||
        context->reserved != 0u || context->host == nullptr || context->paths == nullptr ||
        context->runtime_config == nullptr || context->session_config == nullptr ||
        context->paths->struct_size != sizeof(SntRuntimeHostPathRoots) ||
        context->paths->reserved != 0u ||
        context->runtime_config->struct_size != sizeof(SntRuntimeConfigBlob) ||
        context->session_config->struct_size != sizeof(SntRuntimeConfigBlob) ||
        context->runtime_config->schema_version != 0u ||
        context->runtime_config->payload.size_bytes != 0u ||
        context->session_config->schema_version != 0u ||
        context->session_config->payload.size_bytes != 0u ||
        context->fixed_tick_period_nanoseconds != kSimulationFixedTickPeriodNanoseconds ||
        !runtime_ || !pending_session_) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }

    snt::core::RuntimePaths copied_paths;
    if (!copy_byte_view(context->paths->engine_root_utf8, &copied_paths.engine_root) ||
        !copy_byte_view(context->paths->game_root_utf8, &copied_paths.game_root) ||
        !copy_byte_view(context->paths->user_root_utf8, &copied_paths.user_root)) {
        const snt::core::Error error{snt::core::ErrorCode::kInvalidArgument,
                                     "Zig host supplied invalid runtime path bytes"};
        record_failure(error, "ZigSimulationRuntimeHost::initialize(paths)");
        return status_for_error(error);
    }

    if (auto result = runtime_->init_services(config_, std::move(copied_paths)); !result) {
        auto error = result.error();
        record_failure(error, "ZigSimulationRuntimeHost::initialize(services)");
        return status_for_error(error);
    }
    if (auto result = runtime_->attach_session(std::move(pending_session_)); !result) {
        auto error = result.error();
        record_failure(error, "ZigSimulationRuntimeHost::initialize(session)");
        return status_for_error(error);
    }
    return SNT_ABI_STATUS_OK;
}

SntAbiStatus ZigSimulationRuntimeHost::run_before_fixed_tick(
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context) {
    if (host == nullptr || !valid_tick_context(context) || !runtime_) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    if (auto result = runtime_->begin_host_fixed_tick(
            context->simulation_tick, context->fixed_tick_period_nanoseconds);
        !result) {
        auto error = result.error();
        record_failure(error, "ZigSimulationRuntimeHost::before_fixed_tick");
        return status_for_error(error);
    }
    return propagate_stop_request(host, "ZigSimulationRuntimeHost::before_fixed_tick(stop)");
}

SntAbiStatus ZigSimulationRuntimeHost::run_fixed_systems(
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context) {
    if (host == nullptr || !valid_tick_context(context) || !runtime_) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    if (auto result = runtime_->run_host_fixed_systems(context->simulation_tick); !result) {
        auto error = result.error();
        record_failure(error, "ZigSimulationRuntimeHost::run_fixed_systems");
        return status_for_error(error);
    }
    return propagate_stop_request(host, "ZigSimulationRuntimeHost::run_fixed_systems(stop)");
}

SntAbiStatus ZigSimulationRuntimeHost::run_after_fixed_tick(
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context) {
    if (host == nullptr || !valid_tick_context(context) || !runtime_) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    if (auto result = runtime_->finish_host_fixed_tick(context->simulation_tick); !result) {
        auto error = result.error();
        record_failure(error, "ZigSimulationRuntimeHost::after_fixed_tick");
        return status_for_error(error);
    }
    return propagate_stop_request(host, "ZigSimulationRuntimeHost::after_fixed_tick(stop)");
}

SntAbiStatus ZigSimulationRuntimeHost::propagate_stop_request(SntRuntimeHost* host,
                                                               const char* operation) {
    if (!runtime_ || !runtime_->stop_requested()) return SNT_ABI_STATUS_OK;

    const SntAbiStatus status = snt_runtime_host_request_stop(host);
    if (status != SNT_ABI_STATUS_OK) {
        const snt::core::Error error{error_code_for_status(status),
                                     snt_abi_status_message(status)};
        record_failure(error, operation);
    }
    return status;
}

SntAbiStatus ZigSimulationRuntimeHost::unexpected_callback_exception(
    const char* operation) noexcept {
    try {
        const snt::core::Error error{snt::core::ErrorCode::kUnknown,
                                     "C++ exception escaped a Zig host callback"};
        record_failure(error, operation);
    } catch (...) {
        // Crossing the C ABI with an exception is forbidden. If diagnostics
        // themselves cannot be recorded, the status still stops the host.
    }
    return SNT_ABI_STATUS_INTERNAL_ERROR;
}

void ZigSimulationRuntimeHost::run_shutdown() noexcept {
    try {
        if (runtime_) runtime_->shutdown();
        pending_session_.reset();
        SNT_LOG_INFO("C++ simulation session detached from Zig deterministic host");
    } catch (...) {
        SNT_LOG_ERROR("C++ exception escaped ZigSimulationRuntimeHost shutdown callback");
    }
}

SntAbiStatus ZigSimulationRuntimeHost::on_initialize(
    void* user_data,
    const SntRuntimeSessionInitializeContext* context) noexcept {
    auto* self = static_cast<ZigSimulationRuntimeHost*>(user_data);
    if (self == nullptr) return SNT_ABI_STATUS_INVALID_ARGUMENT;
    try {
        return self->run_initialize(context);
    } catch (...) {
        return self->unexpected_callback_exception("ZigSimulationRuntimeHost::initialize");
    }
}

SntAbiStatus ZigSimulationRuntimeHost::on_before_fixed_tick(
    void* user_data,
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context) noexcept {
    auto* self = static_cast<ZigSimulationRuntimeHost*>(user_data);
    if (self == nullptr) return SNT_ABI_STATUS_INVALID_ARGUMENT;
    try {
        return self->run_before_fixed_tick(host, context);
    } catch (...) {
        return self->unexpected_callback_exception("ZigSimulationRuntimeHost::before_fixed_tick");
    }
}

SntAbiStatus ZigSimulationRuntimeHost::on_fixed_systems(
    void* user_data,
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context) noexcept {
    auto* self = static_cast<ZigSimulationRuntimeHost*>(user_data);
    if (self == nullptr) return SNT_ABI_STATUS_INVALID_ARGUMENT;
    try {
        return self->run_fixed_systems(host, context);
    } catch (...) {
        return self->unexpected_callback_exception("ZigSimulationRuntimeHost::run_fixed_systems");
    }
}

SntAbiStatus ZigSimulationRuntimeHost::on_after_fixed_tick(
    void* user_data,
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context) noexcept {
    auto* self = static_cast<ZigSimulationRuntimeHost*>(user_data);
    if (self == nullptr) return SNT_ABI_STATUS_INVALID_ARGUMENT;
    try {
        return self->run_after_fixed_tick(host, context);
    } catch (...) {
        return self->unexpected_callback_exception("ZigSimulationRuntimeHost::after_fixed_tick");
    }
}

void ZigSimulationRuntimeHost::on_shutdown(void* user_data, SntRuntimeHost*) noexcept {
    auto* self = static_cast<ZigSimulationRuntimeHost*>(user_data);
    if (self != nullptr) self->run_shutdown();
}

snt::core::Expected<void> ZigSimulationRuntimeHost::run_fixed_ticks(uint64_t tick_count) {
    if (host_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ZigSimulationRuntimeHost is not initialized"};
    }

    for (uint64_t index = 0; index < tick_count && !stop_requested(); ++index) {
        SntRuntimeHostState state = SNT_RUNTIME_HOST_STATE_INIT;
        const SntAbiStatus state_status = snt_runtime_host_query_state(host_, &state);
        if (state_status != SNT_ABI_STATUS_OK) {
            return status_error(state_status, "ZigSimulationRuntimeHost::query_state");
        }
        if (state.completed_tick == std::numeric_limits<uint64_t>::max()) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "ZigSimulationRuntimeHost tick counter overflow"};
        }

        SntRuntimeFixedTickResult result = SNT_RUNTIME_FIXED_TICK_RESULT_INIT;
        const SntAbiStatus tick_status =
            snt_runtime_host_run_fixed_tick(host_, state.completed_tick + 1u, &result);
        if (tick_status == SNT_ABI_STATUS_NOT_READY && stop_requested()) {
            break;
        }
        if (tick_status != SNT_ABI_STATUS_OK) {
            if (has_last_error_) return last_error_;
            return status_error(tick_status, "ZigSimulationRuntimeHost::run_fixed_tick");
        }
    }
    return {};
}

void ZigSimulationRuntimeHost::request_stop() noexcept {
    if (runtime_) runtime_->request_stop();
    if (host_ == nullptr) return;

    const SntAbiStatus status = snt_runtime_host_request_stop(host_);
    if (status != SNT_ABI_STATUS_OK) {
        SNT_LOG_WARN("Zig host rejected stop request: %s", snt_abi_status_message(status));
    }
}

bool ZigSimulationRuntimeHost::stop_requested() const noexcept {
    if (!runtime_ || runtime_->stop_requested()) return true;
    if (host_ == nullptr) return true;

    SntRuntimeHostState state = SNT_RUNTIME_HOST_STATE_INIT;
    if (snt_runtime_host_query_state(host_, &state) != SNT_ABI_STATUS_OK) return true;
    return state.lifecycle_state != SNT_RUNTIME_HOST_LIFECYCLE_RUNNING;
}

const SimulationStats& ZigSimulationRuntimeHost::stats() const noexcept {
    return runtime_->stats();
}

const snt::core::Error* ZigSimulationRuntimeHost::last_error() const noexcept {
    return has_last_error_ ? &last_error_ : nullptr;
}

void ZigSimulationRuntimeHost::shutdown() noexcept {
    if (host_ != nullptr) {
        SntRuntimeHost* host = std::exchange(host_, nullptr);
        snt_runtime_host_shutdown(host);
    }
    if (runtime_) runtime_->shutdown();
    pending_session_.reset();
}

}  // namespace snt::engine

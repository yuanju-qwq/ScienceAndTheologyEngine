#include "abi/hash_abi.h"
#include "abi/render_snapshot_abi.h"
#include "abi/runtime_abi.h"
#include "abi/runtime_host_abi.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

extern "C" SntAbiStatus snt_abi_c_smoke_query(SntRuntimeAbiDescriptor* descriptor);
extern "C" uint32_t snt_abi_c_smoke_snapshot_layout_is_valid(void);
extern "C" SntAbiStatus snt_abi_c_smoke_hash(
    const char* text,
    uint64_t text_size,
    uint64_t* out_hash);
extern "C" SntAbiStatus snt_abi_c_smoke_create_host_contract(SntRuntimeHost** out_host);

namespace {

struct RuntimeHostLogCapture {
    uint32_t count = 0;
    SntAbiLogSeverity severity = SNT_ABI_LOG_TRACE;
    const char* channel = nullptr;
    const char* message = nullptr;
};

void capture_runtime_host_log(void* user_data,
                              SntAbiLogSeverity severity,
                              const char* channel,
                              const char* message) {
    auto* capture = static_cast<RuntimeHostLogCapture*>(user_data);
    ++capture->count;
    capture->severity = severity;
    capture->channel = channel;
    capture->message = message;
}

SntAbiStatus runtime_host_initialize(void*, const SntRuntimeSessionInitializeContext*) {
    return SNT_ABI_STATUS_OK;
}

SntAbiStatus runtime_host_apply_command(void*,
                                        SntRuntimeHost*,
                                        const SntRuntimeFixedTickContext*,
                                        const SntRuntimeCommand*) {
    return SNT_ABI_STATUS_OK;
}

SntAbiStatus runtime_host_fixed_tick(void*,
                                     SntRuntimeHost*,
                                     const SntRuntimeFixedTickContext*) {
    return SNT_ABI_STATUS_OK;
}

void runtime_host_shutdown(void*, SntRuntimeHost*) {}

SntRuntimeHostCreateInfo make_runtime_host_create_info(RuntimeHostLogCapture* log_capture) {
    static constexpr char kEngineRoot[] = "engine";
    static constexpr char kGameRoot[] = "game";
    static constexpr char kUserRoot[] = "user";

    SntRuntimeHostCreateInfo create_info = SNT_RUNTIME_HOST_CREATE_INFO_INIT;
    create_info.fixed_tick_period_nanoseconds = UINT64_C(50000000);
    create_info.paths.engine_root_utf8 = {
        reinterpret_cast<const uint8_t*>(kEngineRoot), sizeof(kEngineRoot) - 1u};
    create_info.paths.game_root_utf8 = {
        reinterpret_cast<const uint8_t*>(kGameRoot), sizeof(kGameRoot) - 1u};
    create_info.paths.user_root_utf8 = {
        reinterpret_cast<const uint8_t*>(kUserRoot), sizeof(kUserRoot) - 1u};
    create_info.host_callbacks.user_data = log_capture;
    create_info.host_callbacks.log = capture_runtime_host_log;
    create_info.session_callbacks.initialize = runtime_host_initialize;
    create_info.session_callbacks.apply_command = runtime_host_apply_command;
    create_info.session_callbacks.before_fixed_tick = runtime_host_fixed_tick;
    create_info.session_callbacks.after_fixed_tick = runtime_host_fixed_tick;
    create_info.session_callbacks.shutdown = runtime_host_shutdown;
    return create_info;
}

TEST(RuntimeAbi, DescriptorCanBeQueriedByCConsumer) {
    SntRuntimeAbiDescriptor descriptor{};

    ASSERT_EQ(snt_abi_c_smoke_query(&descriptor), SNT_ABI_STATUS_OK);
    EXPECT_EQ(descriptor.abi_major, SNT_RUNTIME_ABI_MAJOR);
    EXPECT_EQ(descriptor.abi_minor, SNT_RUNTIME_ABI_MINOR);
    EXPECT_EQ(descriptor.runtime_descriptor_size, sizeof(SntRuntimeAbiDescriptor));
    EXPECT_EQ(descriptor.capabilities,
              SNT_RUNTIME_ABI_CAPABILITY_DESCRIPTOR_QUERY |
                  SNT_RUNTIME_ABI_CAPABILITY_HASH_FNV1A64);
    EXPECT_EQ(descriptor.capabilities &
                  (SNT_RUNTIME_ABI_CAPABILITY_HOST_LIFECYCLE |
                   SNT_RUNTIME_ABI_CAPABILITY_DETERMINISTIC_COMMANDS |
                   SNT_RUNTIME_ABI_CAPABILITY_RENDER_SNAPSHOT_LEASES),
              0u);
    EXPECT_STREQ(snt_abi_status_message(SNT_ABI_STATUS_OK), "ok");
}

TEST(RuntimeAbi, RejectsDescriptorsWithoutStablePrefix) {
    SntRuntimeAbiDescriptor descriptor{};
    descriptor.struct_size = static_cast<uint32_t>(
        offsetof(SntRuntimeAbiDescriptor, runtime_descriptor_size));

    EXPECT_EQ(snt_runtime_abi_query_descriptor(&descriptor), SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(snt_runtime_abi_query_descriptor(nullptr), SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_STREQ(snt_abi_status_message(SNT_ABI_STATUS_INVALID_ARGUMENT), "invalid argument");
}

TEST(RenderSnapshotAbi, IsCCompatibleValueOnlyContract) {
    EXPECT_EQ(snt_abi_c_smoke_snapshot_layout_is_valid(), 1u);

    SntRenderSnapshotView snapshot = SNT_RENDER_SNAPSHOT_VIEW_INIT;
    EXPECT_EQ(snapshot.struct_size, sizeof(SntRenderSnapshotView));
    EXPECT_EQ(snapshot.schema_version, SNT_RENDER_SNAPSHOT_SCHEMA_VERSION);
    EXPECT_EQ(snapshot.payload.data, nullptr);
    EXPECT_EQ(snapshot.payload.size_bytes, 0u);

    SntRenderSnapshotLease lease = SNT_RENDER_SNAPSHOT_LEASE_INIT;
    EXPECT_EQ(lease.struct_size, sizeof(SntRenderSnapshotLease));
    EXPECT_EQ(lease.lease_id, SNT_RENDER_SNAPSHOT_LEASE_INVALID);
    EXPECT_EQ(lease.snapshot.struct_size, sizeof(SntRenderSnapshotView));
}

TEST(RuntimeHostAbi, ContractIsDeclaredButNotAdvertisedBeforeAdapter) {
    RuntimeHostLogCapture log_capture;
    SntRuntimeHostCreateInfo create_info = make_runtime_host_create_info(&log_capture);
    auto* host = reinterpret_cast<SntRuntimeHost*>(static_cast<uintptr_t>(1));

    EXPECT_EQ(snt_runtime_host_create(&create_info, &host), SNT_ABI_STATUS_UNSUPPORTED);
    EXPECT_EQ(host, nullptr);
    EXPECT_EQ(log_capture.count, 1u);
    EXPECT_EQ(log_capture.severity, SNT_ABI_LOG_WARN);
    EXPECT_STREQ(log_capture.channel, "runtime_host_abi");
    EXPECT_STREQ(log_capture.message,
                 "SntRuntimeHost contract is declared but no adapter is linked");
}

TEST(RuntimeHostAbi, CConsumerCanConstructTheValueOnlyCreateContract) {
    auto* host = reinterpret_cast<SntRuntimeHost*>(static_cast<uintptr_t>(1));

    EXPECT_EQ(snt_abi_c_smoke_create_host_contract(&host), SNT_ABI_STATUS_UNSUPPORTED);
    EXPECT_EQ(host, nullptr);
}

TEST(RuntimeHostAbi, RejectsReservedCreateFieldsBeforeReportingUnavailable) {
    RuntimeHostLogCapture log_capture;
    SntRuntimeHostCreateInfo create_info = make_runtime_host_create_info(&log_capture);
    create_info.reserved[0] = 1u;
    auto* host = reinterpret_cast<SntRuntimeHost*>(static_cast<uintptr_t>(1));

    EXPECT_EQ(snt_runtime_host_create(&create_info, &host), SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(host, nullptr);
    EXPECT_EQ(log_capture.count, 0u);
}

TEST(RuntimeHostAbi, RejectsCallsWithoutAConcreteHost) {
    SntRuntimeHostState state = SNT_RUNTIME_HOST_STATE_INIT;
    SntRuntimeFixedTickResult tick_result = SNT_RUNTIME_FIXED_TICK_RESULT_INIT;
    SntRenderSnapshotLease lease = SNT_RENDER_SNAPSHOT_LEASE_INIT;

    EXPECT_EQ(snt_runtime_host_query_state(nullptr, &state), SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(snt_runtime_host_run_fixed_tick(nullptr, 1u, &tick_result),
              SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(snt_runtime_host_acquire_render_snapshot(nullptr, &lease),
              SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_STREQ(snt_abi_status_message(SNT_ABI_STATUS_NOT_READY), "operation not ready");
}

TEST(ZigHashAbi, CConsumerUsesTheZigOwnedImplementation) {
    uint64_t hash = 0;
    ASSERT_EQ(snt_abi_c_smoke_hash("foobar", 6u, &hash), SNT_ABI_STATUS_OK);
    EXPECT_EQ(hash, UINT64_C(0x85944171f73967e8));
    EXPECT_EQ(snt_hash_abi_combine(UINT64_C(42), UINT64_C(100)),
              UINT64_C(42) ^
                  (UINT64_C(100) + UINT64_C(0x9e3779b9) + (UINT64_C(42) << 6) +
                   (UINT64_C(42) >> 2)));
}

TEST(ZigHashAbi, RejectsInvalidByteViewsWithoutWritingOutput) {
    uint64_t hash = UINT64_C(0xfeedface);
    const SntAbiByteView invalid_bytes{nullptr, 1u};

    EXPECT_EQ(snt_hash_abi_fnv1a64(invalid_bytes, &hash), SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(hash, UINT64_C(0xfeedface));
    EXPECT_EQ(snt_hash_abi_fnv1a64({nullptr, 0u}, nullptr), SNT_ABI_STATUS_INVALID_ARGUMENT);
}

}  // namespace

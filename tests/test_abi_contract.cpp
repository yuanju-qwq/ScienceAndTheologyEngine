#include "abi/hash_abi.h"
#include "abi/json_abi.h"
#include "abi/render_snapshot_abi.h"
#include "abi/runtime_abi.h"
#include "abi/runtime_host_abi.h"
#include "abi/runtime_key_index_abi.h"
#include "abi/uuid_abi.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

extern "C" SntAbiStatus snt_abi_c_smoke_query(SntRuntimeAbiDescriptor* descriptor);
extern "C" uint32_t snt_abi_c_smoke_snapshot_layout_is_valid(void);
extern "C" SntAbiStatus snt_abi_c_smoke_hash(
    const char* text,
    uint64_t text_size,
    uint64_t* out_hash);
extern "C" SntAbiStatus snt_abi_c_smoke_create_host_contract(SntRuntimeHost** out_host);
extern "C" SntAbiStatus snt_abi_c_smoke_create_runtime_key_index_contract(
    SntRuntimeKeyIndex** out_index);
extern "C" SntAbiStatus snt_abi_c_smoke_uuid_generate(SntUuid* out_uuid);
extern "C" SntAbiStatus snt_abi_c_smoke_json_read_version(uint64_t* out_version);

namespace {

constexpr uint64_t kFixedTickPeriodNanoseconds = UINT64_C(50000000);

enum class RuntimeHostEvent : uint32_t {
    kInitialize,
    kApplyCommand,
    kBeforeFixedTick,
    kRunFixedSystems,
    kAfterFixedTick,
    kShutdown,
};

struct ByteCapture {
    std::array<uint8_t, 64> bytes{};
    uint64_t size = 0;

    bool copy_from(SntAbiByteView view) {
        if ((view.data == nullptr && view.size_bytes != 0u) ||
            view.size_bytes > bytes.size()) {
            return false;
        }

        size = view.size_bytes;
        if (size != 0u) {
            std::memcpy(bytes.data(), view.data, static_cast<size_t>(size));
        }
        return true;
    }
};

struct CommandCapture {
    uint32_t command_type = 0;
    uint32_t schema_version = 0;
    uint64_t target_tick = 0;
    uint64_t producer_high = 0;
    uint64_t producer_low = 0;
    uint64_t sequence = 0;
    ByteCapture payload;
};

struct FixedTickCapture {
    uint64_t simulation_tick = 0;
    uint64_t fixed_tick_period_nanoseconds = 0;
    uint64_t command_count = 0;
};

struct RuntimeHostLogCapture {
    SntAbiLogSeverity severity = SNT_ABI_LOG_TRACE;
    const char* channel = nullptr;
    const char* message = nullptr;
};

struct RuntimeHostProbe {
    std::array<RuntimeHostEvent, 12> events{};
    size_t event_count = 0;
    std::array<CommandCapture, 4> commands{};
    size_t command_count = 0;
    std::array<FixedTickCapture, 3> fixed_ticks{};
    size_t fixed_tick_count = 0;
    std::array<RuntimeHostLogCapture, 4> logs{};
    size_t log_count = 0;

    ByteCapture initialized_engine_root;
    ByteCapture initialized_game_root;
    ByteCapture initialized_user_root;
    ByteCapture initialized_runtime_config;
    ByteCapture initialized_session_config;
    const uint8_t* input_engine_root = nullptr;
    const uint8_t* input_game_root = nullptr;
    const uint8_t* input_user_root = nullptr;
    const uint8_t* input_runtime_config = nullptr;
    const uint8_t* input_session_config = nullptr;
    bool initialize_context_is_valid = false;
    bool initialize_values_are_copied = false;
    SntRuntimeHost* initialized_host = nullptr;
    uint32_t shutdown_count = 0;

    std::array<uint8_t, 8> snapshot_payload{
        static_cast<uint8_t>('s'), static_cast<uint8_t>('n'),
        static_cast<uint8_t>('a'), static_cast<uint8_t>('p'),
        static_cast<uint8_t>('s'), static_cast<uint8_t>('h'),
        static_cast<uint8_t>('o'), static_cast<uint8_t>('t'),
    };
    SntAbiStatus snapshot_publish_status = SNT_ABI_STATUS_INTERNAL_ERROR;
};

struct RuntimeHostCreateInputs {
    std::array<uint8_t, 6> engine_root{
        static_cast<uint8_t>('e'), static_cast<uint8_t>('n'),
        static_cast<uint8_t>('g'), static_cast<uint8_t>('i'),
        static_cast<uint8_t>('n'), static_cast<uint8_t>('e'),
    };
    std::array<uint8_t, 4> game_root{
        static_cast<uint8_t>('g'), static_cast<uint8_t>('a'),
        static_cast<uint8_t>('m'), static_cast<uint8_t>('e'),
    };
    std::array<uint8_t, 4> user_root{
        static_cast<uint8_t>('u'), static_cast<uint8_t>('s'),
        static_cast<uint8_t>('e'), static_cast<uint8_t>('r'),
    };
    std::array<uint8_t, 7> runtime_config{
        static_cast<uint8_t>('r'), static_cast<uint8_t>('u'),
        static_cast<uint8_t>('n'), static_cast<uint8_t>('t'),
        static_cast<uint8_t>('i'), static_cast<uint8_t>('m'),
        static_cast<uint8_t>('e'),
    };
    std::array<uint8_t, 7> session_config{
        static_cast<uint8_t>('s'), static_cast<uint8_t>('e'),
        static_cast<uint8_t>('s'), static_cast<uint8_t>('s'),
        static_cast<uint8_t>('i'), static_cast<uint8_t>('o'),
        static_cast<uint8_t>('n'),
    };
};

template <size_t N>
SntAbiByteView byte_view(const std::array<uint8_t, N>& bytes) {
    return {bytes.data(), static_cast<uint64_t>(bytes.size())};
}

bool record_event(RuntimeHostProbe* probe, RuntimeHostEvent event) {
    if (probe == nullptr || probe->event_count == probe->events.size()) {
        return false;
    }

    probe->events[probe->event_count++] = event;
    return true;
}

bool capture_fixed_tick(RuntimeHostProbe* probe,
                        RuntimeHostEvent event,
                        const SntRuntimeFixedTickContext* context) {
    if (probe == nullptr || context == nullptr ||
        context->struct_size != sizeof(SntRuntimeFixedTickContext) ||
        context->reserved != 0u || probe->fixed_tick_count == probe->fixed_ticks.size() ||
        !record_event(probe, event)) {
        return false;
    }

    probe->fixed_ticks[probe->fixed_tick_count++] = {
        context->simulation_tick,
        context->fixed_tick_period_nanoseconds,
        context->command_count,
    };
    return true;
}

void capture_runtime_host_log(void* user_data,
                              SntAbiLogSeverity severity,
                              const char* channel,
                              const char* message) {
    auto* probe = static_cast<RuntimeHostProbe*>(user_data);
    if (probe == nullptr || probe->log_count == probe->logs.size()) {
        return;
    }

    probe->logs[probe->log_count++] = {severity, channel, message};
}

SntAbiStatus runtime_host_initialize(void* user_data,
                                     const SntRuntimeSessionInitializeContext* context) {
    auto* probe = static_cast<RuntimeHostProbe*>(user_data);
    if (probe == nullptr || context == nullptr ||
        context->struct_size != sizeof(SntRuntimeSessionInitializeContext) ||
        context->reserved != 0u || context->host == nullptr || context->paths == nullptr ||
        context->runtime_config == nullptr || context->session_config == nullptr ||
        context->fixed_tick_period_nanoseconds != kFixedTickPeriodNanoseconds ||
        !record_event(probe, RuntimeHostEvent::kInitialize)) {
        return SNT_ABI_STATUS_INTERNAL_ERROR;
    }

    const auto& paths = *context->paths;
    const auto& runtime_config = *context->runtime_config;
    const auto& session_config = *context->session_config;
    const bool valid_layouts =
        paths.struct_size == sizeof(SntRuntimeHostPathRoots) && paths.reserved == 0u &&
        runtime_config.struct_size == sizeof(SntRuntimeConfigBlob) &&
        session_config.struct_size == sizeof(SntRuntimeConfigBlob) &&
        runtime_config.schema_version == 11u && session_config.schema_version == 12u;
    const bool copied_values =
        paths.engine_root_utf8.data != probe->input_engine_root &&
        paths.game_root_utf8.data != probe->input_game_root &&
        paths.user_root_utf8.data != probe->input_user_root &&
        runtime_config.payload.data != probe->input_runtime_config &&
        session_config.payload.data != probe->input_session_config;
    const bool copied_bytes =
        probe->initialized_engine_root.copy_from(paths.engine_root_utf8) &&
        probe->initialized_game_root.copy_from(paths.game_root_utf8) &&
        probe->initialized_user_root.copy_from(paths.user_root_utf8) &&
        probe->initialized_runtime_config.copy_from(runtime_config.payload) &&
        probe->initialized_session_config.copy_from(session_config.payload);

    probe->initialized_host = context->host;
    probe->initialize_context_is_valid = valid_layouts && copied_bytes;
    probe->initialize_values_are_copied = copied_values;
    return probe->initialize_context_is_valid ? SNT_ABI_STATUS_OK : SNT_ABI_STATUS_INTERNAL_ERROR;
}

SntAbiStatus runtime_host_apply_command(void* user_data,
                                        SntRuntimeHost*,
                                        const SntRuntimeFixedTickContext* context,
                                        const SntRuntimeCommand* command) {
    auto* probe = static_cast<RuntimeHostProbe*>(user_data);
    if (probe == nullptr || context == nullptr || command == nullptr ||
        command->struct_size != sizeof(SntRuntimeCommand) || command->flags != 0u ||
        probe->command_count == probe->commands.size() ||
        !record_event(probe, RuntimeHostEvent::kApplyCommand)) {
        return SNT_ABI_STATUS_INTERNAL_ERROR;
    }

    auto& capture = probe->commands[probe->command_count++];
    capture.command_type = command->command_type;
    capture.schema_version = command->schema_version;
    capture.target_tick = command->target_tick;
    capture.producer_high = command->producer_id.high;
    capture.producer_low = command->producer_id.low;
    capture.sequence = command->sequence;
    if (!capture.payload.copy_from(command->payload)) {
        return SNT_ABI_STATUS_INTERNAL_ERROR;
    }
    return SNT_ABI_STATUS_OK;
}

SntAbiStatus runtime_host_before_fixed_tick(void* user_data,
                                            SntRuntimeHost*,
                                            const SntRuntimeFixedTickContext* context) {
    return capture_fixed_tick(static_cast<RuntimeHostProbe*>(user_data),
                              RuntimeHostEvent::kBeforeFixedTick,
                              context)
               ? SNT_ABI_STATUS_OK
               : SNT_ABI_STATUS_INTERNAL_ERROR;
}

SntAbiStatus runtime_host_run_fixed_systems(void* user_data,
                                            SntRuntimeHost*,
                                            const SntRuntimeFixedTickContext* context) {
    return capture_fixed_tick(static_cast<RuntimeHostProbe*>(user_data),
                              RuntimeHostEvent::kRunFixedSystems,
                              context)
               ? SNT_ABI_STATUS_OK
               : SNT_ABI_STATUS_INTERNAL_ERROR;
}

SntAbiStatus runtime_host_after_fixed_tick(void* user_data,
                                           SntRuntimeHost* host,
                                           const SntRuntimeFixedTickContext* context) {
    auto* probe = static_cast<RuntimeHostProbe*>(user_data);
    if (host == nullptr ||
        !capture_fixed_tick(probe, RuntimeHostEvent::kAfterFixedTick, context)) {
        return SNT_ABI_STATUS_INTERNAL_ERROR;
    }

    SntRenderSnapshotPublishInfo publish_info = SNT_RENDER_SNAPSHOT_PUBLISH_INFO_INIT;
    publish_info.schema_version = 23u;
    publish_info.payload = byte_view(probe->snapshot_payload);
    probe->snapshot_publish_status = snt_runtime_host_publish_render_snapshot(host, &publish_info);
    probe->snapshot_payload[0] = static_cast<uint8_t>('X');
    return probe->snapshot_publish_status;
}

void runtime_host_shutdown(void* user_data, SntRuntimeHost*) {
    auto* probe = static_cast<RuntimeHostProbe*>(user_data);
    if (probe == nullptr) {
        return;
    }

    ++probe->shutdown_count;
    (void)record_event(probe, RuntimeHostEvent::kShutdown);
}

SntRuntimeHostCreateInfo make_runtime_host_create_info(RuntimeHostProbe* probe,
                                                        const RuntimeHostCreateInputs& inputs) {
    SntRuntimeHostCreateInfo create_info = SNT_RUNTIME_HOST_CREATE_INFO_INIT;
    create_info.fixed_tick_period_nanoseconds = kFixedTickPeriodNanoseconds;
    create_info.paths.engine_root_utf8 = byte_view(inputs.engine_root);
    create_info.paths.game_root_utf8 = byte_view(inputs.game_root);
    create_info.paths.user_root_utf8 = byte_view(inputs.user_root);
    create_info.runtime_config.schema_version = 11u;
    create_info.runtime_config.payload = byte_view(inputs.runtime_config);
    create_info.session_config.schema_version = 12u;
    create_info.session_config.payload = byte_view(inputs.session_config);
    create_info.host_callbacks.user_data = probe;
    create_info.host_callbacks.log = capture_runtime_host_log;
    create_info.session_callbacks.user_data = probe;
    create_info.session_callbacks.initialize = runtime_host_initialize;
    create_info.session_callbacks.apply_command = runtime_host_apply_command;
    create_info.session_callbacks.before_fixed_tick = runtime_host_before_fixed_tick;
    create_info.session_callbacks.run_fixed_systems = runtime_host_run_fixed_systems;
    create_info.session_callbacks.after_fixed_tick = runtime_host_after_fixed_tick;
    create_info.session_callbacks.shutdown = runtime_host_shutdown;

    probe->input_engine_root = inputs.engine_root.data();
    probe->input_game_root = inputs.game_root.data();
    probe->input_user_root = inputs.user_root.data();
    probe->input_runtime_config = inputs.runtime_config.data();
    probe->input_session_config = inputs.session_config.data();
    return create_info;
}

template <size_t N>
void expect_captured_bytes(const ByteCapture& capture,
                           const std::array<uint8_t, N>& expected) {
    ASSERT_EQ(capture.size, static_cast<uint64_t>(expected.size()));
    EXPECT_EQ(std::memcmp(capture.bytes.data(), expected.data(), expected.size()), 0);
}

SntRuntimeCommand make_command(uint32_t command_type,
                               uint64_t target_tick,
                               uint64_t producer_high,
                               uint64_t producer_low,
                               uint64_t sequence,
                               SntAbiByteView payload) {
    SntRuntimeCommand command = SNT_RUNTIME_COMMAND_INIT;
    command.command_type = command_type;
    command.schema_version = 1u;
    command.target_tick = target_tick;
    command.producer_id.high = producer_high;
    command.producer_id.low = producer_low;
    command.sequence = sequence;
    command.payload = payload;
    return command;
}

TEST(RuntimeAbi, DescriptorCanBeQueriedByCConsumer) {
    SntRuntimeAbiDescriptor descriptor{};
    constexpr SntRuntimeAbiCapabilities kExpectedCapabilities =
        SNT_RUNTIME_ABI_CAPABILITY_DESCRIPTOR_QUERY |
        SNT_RUNTIME_ABI_CAPABILITY_HASH_FNV1A64 |
        SNT_RUNTIME_ABI_CAPABILITY_HOST_LIFECYCLE |
        SNT_RUNTIME_ABI_CAPABILITY_DETERMINISTIC_COMMANDS |
        SNT_RUNTIME_ABI_CAPABILITY_RENDER_SNAPSHOT_LEASES |
        SNT_RUNTIME_ABI_CAPABILITY_RUNTIME_KEY_INDEX_SNAPSHOTS |
        SNT_RUNTIME_ABI_CAPABILITY_UUID_GENERATOR |
        SNT_RUNTIME_ABI_CAPABILITY_JSON_DOCUMENTS;

    ASSERT_EQ(snt_abi_c_smoke_query(&descriptor), SNT_ABI_STATUS_OK);
    EXPECT_EQ(descriptor.abi_major, SNT_RUNTIME_ABI_MAJOR);
    EXPECT_EQ(descriptor.abi_minor, SNT_RUNTIME_ABI_MINOR);
    EXPECT_EQ(descriptor.runtime_descriptor_size, sizeof(SntRuntimeAbiDescriptor));
    EXPECT_EQ(descriptor.capabilities, kExpectedCapabilities);
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

TEST(RuntimeKeyIndexAbi, CConsumerCanCreateTheZigOwnedIndex) {
    SntRuntimeKeyIndex* index = nullptr;

    ASSERT_EQ(snt_abi_c_smoke_create_runtime_key_index_contract(&index), SNT_ABI_STATUS_OK);
    ASSERT_NE(index, nullptr);
    snt_runtime_key_index_destroy(index);
}

TEST(UuidAbi, CConsumerCanIssueUuidFromTheZigStateMachine) {
    SntUuid uuid = SNT_UUID_INIT;

    ASSERT_EQ(snt_abi_c_smoke_uuid_generate(&uuid), SNT_ABI_STATUS_OK);
    EXPECT_TRUE(uuid.low != 0u || uuid.high != 0u);
    EXPECT_EQ(snt_abi_c_smoke_uuid_generate(nullptr), SNT_ABI_STATUS_INVALID_ARGUMENT);
}

TEST(UuidAbi, DeterministicallyOwnsStateTransitions) {
    SntUuidGeneratorEntropy entropy = SNT_UUID_GENERATOR_ENTROPY_INIT;
    entropy.steady_clock_ticks = UINT64_C(0x1122334455667788);
    entropy.random_words[0] = 1u;
    entropy.random_words[1] = 2u;
    entropy.random_words[2] = 3u;
    entropy.random_words[3] = 4u;

    SntUuidGeneratorState state = SNT_UUID_GENERATOR_STATE_INIT;
    SntUuidGeneratorState matching_state = SNT_UUID_GENERATOR_STATE_INIT;
    ASSERT_EQ(snt_uuid_generator_initialize(&state, &entropy), SNT_ABI_STATUS_OK);
    ASSERT_EQ(snt_uuid_generator_initialize(&matching_state, &entropy), SNT_ABI_STATUS_OK);

    SntUuid first = SNT_UUID_INIT;
    SntUuid matching_first = SNT_UUID_INIT;
    ASSERT_EQ(snt_uuid_generator_next(&state, &first), SNT_ABI_STATUS_OK);
    ASSERT_EQ(snt_uuid_generator_next(&matching_state, &matching_first), SNT_ABI_STATUS_OK);
    EXPECT_EQ(first.low, matching_first.low);
    EXPECT_EQ(first.high, matching_first.high);

    SntUuid peeked = SNT_UUID_INIT;
    SntUuid next = SNT_UUID_INIT;
    ASSERT_EQ(snt_uuid_generator_peek_next(&state, &peeked), SNT_ABI_STATUS_OK);
    ASSERT_EQ(snt_uuid_generator_next(&state, &next), SNT_ABI_STATUS_OK);
    EXPECT_EQ(peeked.low, next.low);
    EXPECT_EQ(peeked.high, next.high);

    ASSERT_EQ(snt_uuid_generator_reset_counter(&state, UINT64_C(100)), SNT_ABI_STATUS_OK);
    SntUuid after_reset = SNT_UUID_INIT;
    ASSERT_EQ(snt_uuid_generator_next(&state, &after_reset), SNT_ABI_STATUS_OK);
    EXPECT_EQ(after_reset.high, first.high);
    EXPECT_EQ(after_reset.low ^ UINT64_C(101), first.low ^ UINT64_C(1));

    SntUuidGeneratorState sentinel_edge_state = SNT_UUID_GENERATOR_STATE_INIT;
    sentinel_edge_state.seed_low = UINT64_C(1);
    SntUuid sentinel_edge_uuid = SNT_UUID_INIT;
    ASSERT_EQ(snt_uuid_generator_reset_counter(&sentinel_edge_state, 0), SNT_ABI_STATUS_OK);
    ASSERT_EQ(snt_uuid_generator_next(&sentinel_edge_state, &sentinel_edge_uuid),
              SNT_ABI_STATUS_OK);
    EXPECT_TRUE(sentinel_edge_uuid.low != 0u || sentinel_edge_uuid.high != 0u);
    EXPECT_EQ(sentinel_edge_uuid.low, UINT64_C(3));
    EXPECT_EQ(sentinel_edge_state.counter, UINT64_C(2));

    SntUuidGeneratorState uninitialized = SNT_UUID_GENERATOR_STATE_INIT;
    EXPECT_EQ(snt_uuid_generator_next(&uninitialized, &after_reset), SNT_ABI_STATUS_INVALID_STATE);
    EXPECT_EQ(snt_uuid_generator_next(nullptr, &after_reset), SNT_ABI_STATUS_INVALID_ARGUMENT);
}

TEST(JsonAbi, CConsumerReadsTheZigOwnedDocument) {
    uint64_t version = 0;

    ASSERT_EQ(snt_abi_c_smoke_json_read_version(&version), SNT_ABI_STATUS_OK);
    EXPECT_EQ(version, 7u);
    EXPECT_EQ(snt_abi_c_smoke_json_read_version(nullptr), SNT_ABI_STATUS_INVALID_ARGUMENT);
}

TEST(JsonAbi, DocumentCopiesInputAndSupportsNestedQueries) {
    std::string source = R"({"name":"zig","items":["one","two"]})";
    SntJsonDocument* document = nullptr;
    const SntAbiByteView source_view{
        reinterpret_cast<const uint8_t*>(source.data()),
        static_cast<uint64_t>(source.size()),
    };
    ASSERT_EQ(snt_json_document_parse(source_view, &document), SNT_ABI_STATUS_OK);
    ASSERT_NE(document, nullptr);

    source[source.find("zig")] = 'X';

    const SntJsonValue* root = nullptr;
    ASSERT_EQ(snt_json_document_root(document, &root), SNT_ABI_STATUS_OK);
    ASSERT_NE(root, nullptr);

    const char kName[] = "name";
    const SntAbiByteView name_key{
        reinterpret_cast<const uint8_t*>(kName), sizeof(kName) - 1u,
    };
    const SntJsonValue* name = nullptr;
    ASSERT_EQ(snt_json_object_find(root, name_key, &name), SNT_ABI_STATUS_OK);
    ASSERT_NE(name, nullptr);
    SntAbiByteView name_text{nullptr, 0u};
    ASSERT_EQ(snt_json_value_read_string(name, &name_text), SNT_ABI_STATUS_OK);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(name_text.data),
                          static_cast<size_t>(name_text.size_bytes)),
              "zig");

    const char kItems[] = "items";
    const SntAbiByteView items_key{
        reinterpret_cast<const uint8_t*>(kItems), sizeof(kItems) - 1u,
    };
    const SntJsonValue* items = nullptr;
    ASSERT_EQ(snt_json_object_find(root, items_key, &items), SNT_ABI_STATUS_OK);
    uint64_t item_count = 0;
    ASSERT_EQ(snt_json_array_count(items, &item_count), SNT_ABI_STATUS_OK);
    EXPECT_EQ(item_count, 2u);
    const SntJsonValue* second_item = nullptr;
    ASSERT_EQ(snt_json_array_get(items, 1u, &second_item), SNT_ABI_STATUS_OK);
    SntAbiByteView second_text{nullptr, 0u};
    ASSERT_EQ(snt_json_value_read_string(second_item, &second_text), SNT_ABI_STATUS_OK);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(second_text.data),
                          static_cast<size_t>(second_text.size_bytes)),
              "two");

    const char kMissing[] = "missing";
    const SntAbiByteView missing_key{
        reinterpret_cast<const uint8_t*>(kMissing), sizeof(kMissing) - 1u,
    };
    const SntJsonValue* missing = nullptr;
    EXPECT_EQ(snt_json_object_find(root, missing_key, &missing), SNT_ABI_STATUS_OK);
    EXPECT_EQ(missing, nullptr);
    EXPECT_EQ(snt_json_document_parse({nullptr, 1u}, &document),
              SNT_ABI_STATUS_INVALID_ARGUMENT);

    snt_json_document_destroy(document);
}

TEST(RuntimeKeyIndexAbi, CopiesKeysAndKeepsAcquiredSnapshotAliveAfterIndexDestruction) {
    SntRuntimeKeyIndexCreateInfo create_info = SNT_RUNTIME_KEY_INDEX_CREATE_INFO_INIT;
    SntRuntimeKeyIndex* index = nullptr;
    ASSERT_EQ(snt_runtime_key_index_create(&create_info, &index), SNT_ABI_STATUS_OK);
    ASSERT_NE(index, nullptr);

    std::array<uint8_t, 4> zinc{
        static_cast<uint8_t>('z'), static_cast<uint8_t>('i'),
        static_cast<uint8_t>('n'), static_cast<uint8_t>('c'),
    };
    const std::array<uint8_t, 8> charcoal{
        static_cast<uint8_t>('c'), static_cast<uint8_t>('h'),
        static_cast<uint8_t>('a'), static_cast<uint8_t>('r'),
        static_cast<uint8_t>('c'), static_cast<uint8_t>('o'),
        static_cast<uint8_t>('a'), static_cast<uint8_t>('l'),
    };
    const std::array<uint8_t, 4> iron{
        static_cast<uint8_t>('i'), static_cast<uint8_t>('r'),
        static_cast<uint8_t>('o'), static_cast<uint8_t>('n'),
    };
    const std::array<SntAbiByteView, 3> keys{
        byte_view(zinc), byte_view(charcoal), byte_view(iron),
    };
    ASSERT_EQ(snt_runtime_key_index_rebuild(
                  index, keys.data(), static_cast<uint64_t>(keys.size())),
              SNT_ABI_STATUS_OK);
    zinc[0] = static_cast<uint8_t>('X');

    SntRuntimeKeyIndexSnapshot* snapshot = nullptr;
    ASSERT_EQ(snt_runtime_key_index_acquire_snapshot(index, &snapshot), SNT_ABI_STATUS_OK);
    ASSERT_NE(snapshot, nullptr);
    ASSERT_EQ(snt_runtime_key_index_snapshot_retain(snapshot), SNT_ABI_STATUS_OK);

    const std::array<SntAbiByteView, 2> duplicate_keys{
        byte_view(iron), byte_view(iron),
    };
    EXPECT_EQ(snt_runtime_key_index_rebuild(
                  index, duplicate_keys.data(), static_cast<uint64_t>(duplicate_keys.size())),
              SNT_ABI_STATUS_INVALID_ARGUMENT);

    SntRuntimeKeyIndexSnapshotInfo info = SNT_RUNTIME_KEY_INDEX_SNAPSHOT_INFO_INIT;
    ASSERT_EQ(snt_runtime_key_index_snapshot_query(snapshot, &info), SNT_ABI_STATUS_OK);
    EXPECT_EQ(info.generation, 1u);
    EXPECT_EQ(info.key_count, 3u);

    const std::array<uint8_t, 4> zinc_lookup{
        static_cast<uint8_t>('z'), static_cast<uint8_t>('i'),
        static_cast<uint8_t>('n'), static_cast<uint8_t>('c'),
    };
    SntRuntimeKeyId zinc_id = SNT_RUNTIME_KEY_ID_INVALID;
    ASSERT_EQ(snt_runtime_key_index_snapshot_find_id(
                  snapshot, byte_view(zinc_lookup), &zinc_id),
              SNT_ABI_STATUS_OK);
    EXPECT_EQ(zinc_id, 3u);

    snt_runtime_key_index_destroy(index);

    SntAbiByteView found_key{nullptr, 0u};
    ASSERT_EQ(snt_runtime_key_index_snapshot_find_key(snapshot, zinc_id, &found_key),
              SNT_ABI_STATUS_OK);
    ASSERT_NE(found_key.data, nullptr);
    ASSERT_EQ(found_key.size_bytes, zinc_lookup.size());
    EXPECT_EQ(std::memcmp(found_key.data, zinc_lookup.data(), zinc_lookup.size()), 0);

    snt_runtime_key_index_snapshot_release(snapshot);
    snt_runtime_key_index_snapshot_release(snapshot);
}

TEST(RuntimeHostAbi, ZigHostOwnsDeterministicLifecycleAndSnapshotLeases) {
    RuntimeHostProbe probe;
    const RuntimeHostCreateInputs create_inputs;
    SntRuntimeHostCreateInfo create_info = make_runtime_host_create_info(&probe, create_inputs);
    SntRuntimeHost* host = nullptr;

    ASSERT_EQ(snt_runtime_host_create(&create_info, &host), SNT_ABI_STATUS_OK);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(probe.initialized_host, host);
    EXPECT_TRUE(probe.initialize_context_is_valid);
    EXPECT_TRUE(probe.initialize_values_are_copied);
    expect_captured_bytes(probe.initialized_engine_root, create_inputs.engine_root);
    expect_captured_bytes(probe.initialized_game_root, create_inputs.game_root);
    expect_captured_bytes(probe.initialized_user_root, create_inputs.user_root);
    expect_captured_bytes(probe.initialized_runtime_config, create_inputs.runtime_config);
    expect_captured_bytes(probe.initialized_session_config, create_inputs.session_config);
    ASSERT_EQ(probe.log_count, 1u);
    EXPECT_EQ(probe.logs[0].severity, SNT_ABI_LOG_INFO);
    EXPECT_STREQ(probe.logs[0].channel, "runtime_host");
    EXPECT_STREQ(probe.logs[0].message, "SntRuntimeHost initialized with Zig deterministic core");

    SntRenderSnapshotLease unavailable_lease = SNT_RENDER_SNAPSHOT_LEASE_INIT;
    EXPECT_EQ(snt_runtime_host_acquire_render_snapshot(host, &unavailable_lease),
              SNT_ABI_STATUS_NOT_READY);

    constexpr std::array<uint8_t, 3> kFirstPayload{
        static_cast<uint8_t>('o'), static_cast<uint8_t>('n'), static_cast<uint8_t>('e')};
    constexpr std::array<uint8_t, 3> kSecondPayload{
        static_cast<uint8_t>('t'), static_cast<uint8_t>('w'), static_cast<uint8_t>('o')};
    constexpr std::array<uint8_t, 5> kThirdPayload{
        static_cast<uint8_t>('t'), static_cast<uint8_t>('h'), static_cast<uint8_t>('r'),
        static_cast<uint8_t>('e'), static_cast<uint8_t>('e')};
    auto first_payload = kFirstPayload;
    auto second_payload = kSecondPayload;
    auto third_payload = kThirdPayload;
    const SntRuntimeCommand third = make_command(
        30u, 1u, 1u, 0u, 2u, byte_view(third_payload));
    const SntRuntimeCommand first = make_command(
        10u, 1u, 0u, 5u, 9u, byte_view(first_payload));
    const SntRuntimeCommand second = make_command(
        20u, 1u, 1u, 0u, 1u, byte_view(second_payload));

    ASSERT_EQ(snt_runtime_host_enqueue_command(host, &third), SNT_ABI_STATUS_OK);
    ASSERT_EQ(snt_runtime_host_enqueue_command(host, &first), SNT_ABI_STATUS_OK);
    ASSERT_EQ(snt_runtime_host_enqueue_command(host, &second), SNT_ABI_STATUS_OK);
    first_payload[0] = static_cast<uint8_t>('X');
    second_payload[0] = static_cast<uint8_t>('X');
    third_payload[0] = static_cast<uint8_t>('X');

    EXPECT_EQ(snt_runtime_host_enqueue_command(host, &first), SNT_ABI_STATUS_INVALID_ARGUMENT);
    SntRuntimeCommand invalid_flags = make_command(40u, 1u, 2u, 0u, 1u, byte_view(first_payload));
    invalid_flags.flags = 1u;
    EXPECT_EQ(snt_runtime_host_enqueue_command(host, &invalid_flags), SNT_ABI_STATUS_INVALID_ARGUMENT);

    SntRuntimeFixedTickResult tick_result = SNT_RUNTIME_FIXED_TICK_RESULT_INIT;
    ASSERT_EQ(snt_runtime_host_run_fixed_tick(host, 1u, &tick_result), SNT_ABI_STATUS_OK);
    EXPECT_EQ(tick_result.lifecycle_state, SNT_RUNTIME_HOST_LIFECYCLE_RUNNING);
    EXPECT_EQ(tick_result.completed_tick, 1u);
    EXPECT_EQ(tick_result.applied_command_count, 3u);
    EXPECT_EQ(tick_result.latest_snapshot_sequence, 1u);

    const std::array<RuntimeHostEvent, 7> expected_events{
        RuntimeHostEvent::kInitialize,
        RuntimeHostEvent::kApplyCommand,
        RuntimeHostEvent::kApplyCommand,
        RuntimeHostEvent::kApplyCommand,
        RuntimeHostEvent::kBeforeFixedTick,
        RuntimeHostEvent::kRunFixedSystems,
        RuntimeHostEvent::kAfterFixedTick,
    };
    ASSERT_EQ(probe.event_count, expected_events.size());
    for (size_t index = 0; index < expected_events.size(); ++index) {
        EXPECT_EQ(static_cast<uint32_t>(probe.events[index]),
                  static_cast<uint32_t>(expected_events[index]));
    }

    ASSERT_EQ(probe.command_count, 3u);
    EXPECT_EQ(probe.commands[0].command_type, 10u);
    EXPECT_EQ(probe.commands[0].producer_high, 0u);
    EXPECT_EQ(probe.commands[0].producer_low, 5u);
    EXPECT_EQ(probe.commands[0].sequence, 9u);
    expect_captured_bytes(probe.commands[0].payload, kFirstPayload);
    EXPECT_EQ(probe.commands[1].command_type, 20u);
    EXPECT_EQ(probe.commands[1].producer_high, 1u);
    EXPECT_EQ(probe.commands[1].producer_low, 0u);
    EXPECT_EQ(probe.commands[1].sequence, 1u);
    expect_captured_bytes(probe.commands[1].payload, kSecondPayload);
    EXPECT_EQ(probe.commands[2].command_type, 30u);
    EXPECT_EQ(probe.commands[2].producer_high, 1u);
    EXPECT_EQ(probe.commands[2].producer_low, 0u);
    EXPECT_EQ(probe.commands[2].sequence, 2u);
    expect_captured_bytes(probe.commands[2].payload, kThirdPayload);

    ASSERT_EQ(probe.fixed_tick_count, 3u);
    for (size_t index = 0; index < probe.fixed_tick_count; ++index) {
        EXPECT_EQ(probe.fixed_ticks[index].simulation_tick, 1u);
        EXPECT_EQ(probe.fixed_ticks[index].fixed_tick_period_nanoseconds,
                  kFixedTickPeriodNanoseconds);
        EXPECT_EQ(probe.fixed_ticks[index].command_count, 3u);
    }
    EXPECT_EQ(probe.snapshot_publish_status, SNT_ABI_STATUS_OK);

    SntRenderSnapshotLease lease = SNT_RENDER_SNAPSHOT_LEASE_INIT;
    ASSERT_EQ(snt_runtime_host_acquire_render_snapshot(host, &lease), SNT_ABI_STATUS_OK);
    EXPECT_NE(lease.lease_id, SNT_RENDER_SNAPSHOT_LEASE_INVALID);
    EXPECT_EQ(lease.snapshot.schema_version, 23u);
    EXPECT_EQ(lease.snapshot.simulation_tick, 1u);
    EXPECT_EQ(lease.snapshot.presentation_sequence, 1u);
    constexpr std::array<uint8_t, 8> kExpectedSnapshot{
        static_cast<uint8_t>('s'), static_cast<uint8_t>('n'),
        static_cast<uint8_t>('a'), static_cast<uint8_t>('p'),
        static_cast<uint8_t>('s'), static_cast<uint8_t>('h'),
        static_cast<uint8_t>('o'), static_cast<uint8_t>('t'),
    };
    ASSERT_EQ(lease.snapshot.payload.size_bytes,
              static_cast<uint64_t>(kExpectedSnapshot.size()));
    EXPECT_EQ(std::memcmp(lease.snapshot.payload.data,
                          kExpectedSnapshot.data(),
                          kExpectedSnapshot.size()),
              0);
    EXPECT_EQ(snt_runtime_host_release_render_snapshot(host, lease.lease_id), SNT_ABI_STATUS_OK);
    EXPECT_EQ(snt_runtime_host_release_render_snapshot(host, lease.lease_id),
              SNT_ABI_STATUS_INVALID_ARGUMENT);

    SntRuntimeHostState state = SNT_RUNTIME_HOST_STATE_INIT;
    ASSERT_EQ(snt_runtime_host_query_state(host, &state), SNT_ABI_STATUS_OK);
    EXPECT_EQ(state.lifecycle_state, SNT_RUNTIME_HOST_LIFECYCLE_RUNNING);
    EXPECT_EQ(state.completed_tick, 1u);
    EXPECT_EQ(state.queued_command_count, 0u);
    EXPECT_EQ(state.latest_snapshot_sequence, 1u);

    const SntRuntimeCommand past = make_command(50u, 1u, 9u, 0u, 1u, byte_view(kFirstPayload));
    EXPECT_EQ(snt_runtime_host_enqueue_command(host, &past), SNT_ABI_STATUS_INVALID_STATE);
    EXPECT_EQ(snt_runtime_host_request_stop(host), SNT_ABI_STATUS_OK);
    ASSERT_EQ(snt_runtime_host_query_state(host, &state), SNT_ABI_STATUS_OK);
    EXPECT_EQ(state.lifecycle_state, SNT_RUNTIME_HOST_LIFECYCLE_STOP_REQUESTED);
    tick_result = SNT_RUNTIME_FIXED_TICK_RESULT_INIT;
    EXPECT_EQ(snt_runtime_host_run_fixed_tick(host, 2u, &tick_result), SNT_ABI_STATUS_NOT_READY);
    EXPECT_EQ(tick_result.lifecycle_state, SNT_RUNTIME_HOST_LIFECYCLE_STOP_REQUESTED);
    EXPECT_EQ(tick_result.completed_tick, 1u);

    snt_runtime_host_shutdown(host);
    EXPECT_EQ(probe.shutdown_count, 1u);
    ASSERT_EQ(probe.event_count, expected_events.size() + 1u);
    EXPECT_EQ(static_cast<uint32_t>(probe.events[probe.event_count - 1u]),
              static_cast<uint32_t>(RuntimeHostEvent::kShutdown));
    ASSERT_EQ(probe.log_count, 2u);
    EXPECT_EQ(probe.logs[1].severity, SNT_ABI_LOG_INFO);
    EXPECT_STREQ(probe.logs[1].channel, "runtime_host");
    EXPECT_STREQ(probe.logs[1].message, "SntRuntimeHost shutting down Zig deterministic core");
}

TEST(RuntimeHostAbi, CConsumerCanCreateAndCloseTheConcreteHost) {
    SntRuntimeHost* host = nullptr;

    ASSERT_EQ(snt_abi_c_smoke_create_host_contract(&host), SNT_ABI_STATUS_OK);
    ASSERT_NE(host, nullptr);
    snt_runtime_host_shutdown(host);
}

TEST(RuntimeHostAbi, RejectsReservedCreateFieldsBeforeHostAllocation) {
    RuntimeHostProbe probe;
    const RuntimeHostCreateInputs inputs;
    SntRuntimeHostCreateInfo create_info = make_runtime_host_create_info(&probe, inputs);
    create_info.reserved[0] = 1u;
    auto* host = reinterpret_cast<SntRuntimeHost*>(static_cast<uintptr_t>(1));

    EXPECT_EQ(snt_runtime_host_create(&create_info, &host), SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(host, nullptr);
    EXPECT_EQ(probe.log_count, 0u);
    EXPECT_EQ(probe.event_count, 0u);
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
    EXPECT_EQ(snt_runtime_host_release_render_snapshot(nullptr, 1u),
              SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(snt_runtime_host_request_stop(nullptr), SNT_ABI_STATUS_INVALID_ARGUMENT);
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

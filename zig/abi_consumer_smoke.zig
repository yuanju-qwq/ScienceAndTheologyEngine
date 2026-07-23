// Zig-side proof that one snt_abi static archive exposes the complete public
// contract without depending on C++ or a second Zig implementation archive.

const std = @import("std");
const c = @cImport({
    @cInclude("abi/hash_abi.h");
    @cInclude("abi/runtime_abi.h");
    @cInclude("abi/runtime_host_abi.h");
    @cInclude("abi/runtime_key_index_abi.h");
    @cInclude("abi/uuid_abi.h");
});

test "one snt_abi archive serves a Zig C ABI consumer" {
    var descriptor = std.mem.zeroes(c.SntRuntimeAbiDescriptor);
    descriptor.struct_size = @sizeOf(c.SntRuntimeAbiDescriptor);
    try std.testing.expectEqual(
        @as(c_uint, c.SNT_ABI_STATUS_OK),
        c.snt_runtime_abi_query_descriptor(&descriptor),
    );
    try std.testing.expectEqual(
        @as(c_ulonglong, c.SNT_RUNTIME_ABI_CAPABILITY_DESCRIPTOR_QUERY |
            c.SNT_RUNTIME_ABI_CAPABILITY_HASH_FNV1A64 |
            c.SNT_RUNTIME_ABI_CAPABILITY_HOST_LIFECYCLE |
            c.SNT_RUNTIME_ABI_CAPABILITY_DETERMINISTIC_COMMANDS |
            c.SNT_RUNTIME_ABI_CAPABILITY_RENDER_SNAPSHOT_LEASES |
            c.SNT_RUNTIME_ABI_CAPABILITY_RUNTIME_KEY_INDEX_SNAPSHOTS |
            c.SNT_RUNTIME_ABI_CAPABILITY_UUID_GENERATOR),
        descriptor.capabilities,
    );

    const text = "foobar";
    const bytes = c.SntAbiByteView{
        .data = text.ptr,
        .size_bytes = text.len,
    };
    var hash: u64 = 0;
    try std.testing.expectEqual(
        @as(c_uint, c.SNT_ABI_STATUS_OK),
        c.snt_hash_abi_fnv1a64(bytes, &hash),
    );
    try std.testing.expectEqual(@as(u64, 0x85944171f73967e8), hash);

    var host_state = std.mem.zeroes(c.SntRuntimeHostState);
    host_state.struct_size = @sizeOf(c.SntRuntimeHostState);
    try std.testing.expectEqual(
        @as(c_uint, c.SNT_ABI_STATUS_INVALID_ARGUMENT),
        c.snt_runtime_host_query_state(null, &host_state),
    );

    var snapshot_lease = std.mem.zeroes(c.SntRenderSnapshotLease);
    snapshot_lease.struct_size = @sizeOf(c.SntRenderSnapshotLease);
    try std.testing.expectEqual(
        @as(c_uint, c.SNT_ABI_STATUS_INVALID_ARGUMENT),
        c.snt_runtime_host_acquire_render_snapshot(null, &snapshot_lease),
    );

    var key_index_create_info = std.mem.zeroes(c.SntRuntimeKeyIndexCreateInfo);
    key_index_create_info.struct_size = @sizeOf(c.SntRuntimeKeyIndexCreateInfo);
    var key_index: ?*c.SntRuntimeKeyIndex = null;
    try std.testing.expectEqual(
        @as(c_uint, c.SNT_ABI_STATUS_OK),
        c.snt_runtime_key_index_create(&key_index_create_info, &key_index),
    );
    try std.testing.expect(key_index != null);
    c.snt_runtime_key_index_destroy(key_index);

    var uuid_entropy = std.mem.zeroes(c.SntUuidGeneratorEntropy);
    uuid_entropy.struct_size = @sizeOf(c.SntUuidGeneratorEntropy);
    uuid_entropy.steady_clock_ticks = 0x1122334455667788;
    uuid_entropy.random_words = .{ 1, 2, 3, 4 };
    var uuid_state = std.mem.zeroes(c.SntUuidGeneratorState);
    uuid_state.struct_size = @sizeOf(c.SntUuidGeneratorState);
    var uuid = c.SntUuid{ .low = 0, .high = 0 };
    try std.testing.expectEqual(
        @as(c_uint, c.SNT_ABI_STATUS_OK),
        c.snt_uuid_generator_initialize(&uuid_state, &uuid_entropy),
    );
    try std.testing.expectEqual(
        @as(c_uint, c.SNT_ABI_STATUS_OK),
        c.snt_uuid_generator_next(&uuid_state, &uuid),
    );
    try std.testing.expect(uuid.low != 0 or uuid.high != 0);
}

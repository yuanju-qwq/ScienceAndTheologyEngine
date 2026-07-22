// Zig-side proof that one snt_abi static archive exposes the complete public
// contract without depending on C++ or a second Zig implementation archive.

const std = @import("std");
const c = @cImport({
    @cInclude("abi/hash_abi.h");
    @cInclude("abi/runtime_abi.h");
    @cInclude("abi/runtime_host_abi.h");
});

test "one snt_abi archive serves a Zig C ABI consumer" {
    var descriptor = std.mem.zeroes(c.SntRuntimeAbiDescriptor);
    descriptor.struct_size = @sizeOf(c.SntRuntimeAbiDescriptor);
    try std.testing.expectEqual(
        @as(c_uint, c.SNT_ABI_STATUS_OK),
        c.snt_runtime_abi_query_descriptor(&descriptor),
    );
    try std.testing.expectEqual(
        @as(c_ulonglong, c.SNT_RUNTIME_ABI_CAPABILITY_DESCRIPTOR_QUERY | c.SNT_RUNTIME_ABI_CAPABILITY_HASH_FNV1A64),
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
}

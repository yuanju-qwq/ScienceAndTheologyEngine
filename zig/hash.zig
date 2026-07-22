// Allocation-free hash implementation for the language-neutral engine ABI.
//
// This module intentionally owns no allocator, global state, or logging path.
// Its exported functions remain C ABI so the C++ engine and future Zig hosts
// never need to depend on a Zig-internal ABI.

const std = @import("std");

const status_ok: u32 = 0;
const status_invalid_argument: u32 = 1;
const fnv_offset_basis: u64 = 0xcbf29ce484222325;
const fnv_prime: u64 = 0x100000001b3;

// Mirrors SntAbiByteView exactly. Keep this external layout in sync with the
// C header: Windows x64 passes a 16-byte aggregate differently from separate
// pointer/length parameters.
const AbiByteView = extern struct {
    data: ?[*]const u8,
    size_bytes: u64,
};

pub export fn snt_hash_abi_fnv1a64(
    bytes: AbiByteView,
    out_hash: ?*u64,
) callconv(.c) u32 {
    const output = out_hash orelse return status_invalid_argument;
    if (bytes.size_bytes != 0 and bytes.data == null) {
        return status_invalid_argument;
    }
    if (bytes.size_bytes > @as(u64, std.math.maxInt(usize))) {
        return status_invalid_argument;
    }

    var hash = fnv_offset_basis;
    const byte_count: usize = @intCast(bytes.size_bytes);
    if (byte_count != 0) {
        const input = bytes.data.?;
        for (input[0..byte_count]) |byte| {
            hash ^= @as(u64, byte);
            hash = hash *% fnv_prime;
        }
    }

    output.* = hash;
    return status_ok;
}

pub export fn snt_hash_abi_combine(seed: u64, value: u64) callconv(.c) u64 {
    const mixed = (value +% 0x9e3779b9) +% (seed << 6) +% (seed >> 2);
    return seed ^ mixed;
}

test "FNV-1a matches stable engine vectors" {
    var hash: u64 = 0;
    try std.testing.expectEqual(
        status_ok,
        snt_hash_abi_fnv1a64(.{ .data = null, .size_bytes = 0 }, &hash),
    );
    try std.testing.expectEqual(fnv_offset_basis, hash);

    const foobar = "foobar";
    try std.testing.expectEqual(
        status_ok,
        snt_hash_abi_fnv1a64(.{
            .data = foobar.ptr,
            .size_bytes = @intCast(foobar.len),
        }, &hash),
    );
    try std.testing.expectEqual(@as(u64, 0x85944171f73967e8), hash);
}

test "FNV-1a rejects invalid C ABI arguments" {
    var hash: u64 = 0;
    try std.testing.expectEqual(
        status_invalid_argument,
        snt_hash_abi_fnv1a64(.{ .data = null, .size_bytes = 1 }, &hash),
    );
    try std.testing.expectEqual(
        status_invalid_argument,
        snt_hash_abi_fnv1a64(.{ .data = null, .size_bytes = 0 }, null),
    );
}

test "hash combine preserves the established mixer" {
    const seed: u64 = 0x1234567890abcdef;
    const value: u64 = 0xaabbccdd;
    try std.testing.expectEqual(
        seed ^ (value +% 0x9e3779b9 +% (seed << 6) +% (seed >> 2)),
        snt_hash_abi_combine(seed, value),
    );
}

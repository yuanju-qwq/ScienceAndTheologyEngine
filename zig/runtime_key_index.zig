// Zig-owned deterministic runtime StringKey-to-ID index.
//
// The C ABI exposes opaque immutable snapshots. Rebuilds copy and validate all
// incoming bytes before publishing a replacement, while each snapshot carries
// its own allocator and atomic reference count so it can outlive the index or
// be released by worker code after a content reload.

const std = @import("std");
const c = @cImport({
    @cInclude("abi/runtime_key_index_abi.h");
});

const Allocator = std.mem.Allocator;
const Status = c.SntAbiStatus;

const status_ok: Status = c.SNT_ABI_STATUS_OK;
const status_invalid_argument: Status = c.SNT_ABI_STATUS_INVALID_ARGUMENT;
const status_internal_error: Status = c.SNT_ABI_STATUS_INTERNAL_ERROR;
const status_invalid_state: Status = c.SNT_ABI_STATUS_INVALID_STATE;

const Key = struct {
    bytes: []u8,
};

const Snapshot = struct {
    allocator: Allocator,
    ref_count: std.atomic.Value(u64),
    generation: u64,
    keys: []Key,
};

const Index = struct {
    allocator: Allocator,
    create_info: c.SntRuntimeKeyIndexCreateInfo,
    current_snapshot: *Snapshot,
};

const BuildError = error{
    InvalidArgument,
    OutOfMemory,
};

fn supportsStruct(comptime T: type, supplied_size: anytype) bool {
    return @as(usize, @intCast(supplied_size)) >= @sizeOf(T);
}

fn validCreateInfo(info: c.SntRuntimeKeyIndexCreateInfo) bool {
    return supportsStruct(c.SntRuntimeKeyIndexCreateInfo, info.struct_size) and
        info.reserved == 0 and
        info.reserved_words[0] == 0 and info.reserved_words[1] == 0 and
        info.reserved_words[2] == 0 and info.reserved_words[3] == 0;
}

fn byteViewSlice(view: c.SntAbiByteView) ?[]const u8 {
    if (view.size_bytes > @as(u64, @intCast(std.math.maxInt(usize)))) return null;
    const byte_count: usize = @intCast(view.size_bytes);
    if (byte_count == 0) return &[_]u8{};
    const data = view.data orelse return null;
    return data[0..byte_count];
}

fn validKeyBytes(bytes: []const u8) bool {
    return bytes.len != 0 and std.mem.indexOfScalar(u8, bytes, 0) == null;
}

fn keyLessThan(_: void, left: Key, right: Key) bool {
    return std.mem.lessThan(u8, left.bytes, right.bytes);
}

fn destroySnapshot(snapshot: *Snapshot) void {
    for (snapshot.keys) |key| snapshot.allocator.free(key.bytes);
    snapshot.allocator.free(snapshot.keys);
    snapshot.allocator.destroy(snapshot);
}

fn retainSnapshot(snapshot: *Snapshot) bool {
    var observed = snapshot.ref_count.load(.acquire);
    while (true) {
        if (observed == std.math.maxInt(u64)) return false;
        if (snapshot.ref_count.cmpxchgWeak(
            observed,
            observed + 1,
            .acq_rel,
            .acquire,
        )) |actual| {
            observed = actual;
        } else {
            return true;
        }
    }
}

fn releaseSnapshot(snapshot: *Snapshot) void {
    const prior = snapshot.ref_count.fetchSub(1, .acq_rel);
    std.debug.assert(prior != 0);
    if (prior == 1) destroySnapshot(snapshot);
}

fn buildSnapshot(
    allocator: Allocator,
    input_keys: []const c.SntAbiByteView,
    generation: u64,
) BuildError!*Snapshot {
    const snapshot = allocator.create(Snapshot) catch return error.OutOfMemory;
    errdefer allocator.destroy(snapshot);

    const keys = allocator.alloc(Key, input_keys.len) catch return error.OutOfMemory;
    var initialized_key_count: usize = 0;
    errdefer {
        for (keys[0..initialized_key_count]) |key| allocator.free(key.bytes);
        allocator.free(keys);
    }

    for (input_keys, 0..) |view, index| {
        const bytes = byteViewSlice(view) orelse return error.InvalidArgument;
        if (!validKeyBytes(bytes)) return error.InvalidArgument;
        keys[index] = .{
            .bytes = allocator.dupe(u8, bytes) catch return error.OutOfMemory,
        };
        initialized_key_count += 1;
    }

    std.mem.sort(Key, keys, {}, keyLessThan);
    var index: usize = 1;
    while (index < keys.len) : (index += 1) {
        if (std.mem.eql(u8, keys[index - 1].bytes, keys[index].bytes)) {
            return error.InvalidArgument;
        }
    }

    snapshot.* = .{
        .allocator = allocator,
        .ref_count = std.atomic.Value(u64).init(1),
        .generation = generation,
        .keys = keys,
    };
    return snapshot;
}

fn findKeyIndex(snapshot: *const Snapshot, key: []const u8) ?usize {
    var lower: usize = 0;
    var upper: usize = snapshot.keys.len;
    while (lower < upper) {
        const middle = lower + (upper - lower) / 2;
        if (std.mem.lessThan(u8, snapshot.keys[middle].bytes, key)) {
            lower = middle + 1;
        } else {
            upper = middle;
        }
    }
    if (lower < snapshot.keys.len and std.mem.eql(u8, snapshot.keys[lower].bytes, key)) {
        return lower;
    }
    return null;
}

fn indexToC(index: *Index) *c.SntRuntimeKeyIndex {
    return @ptrCast(index);
}

fn indexFromC(raw_index: ?*c.SntRuntimeKeyIndex) ?*Index {
    const index = raw_index orelse return null;
    return @ptrCast(@alignCast(index));
}

fn snapshotToC(snapshot: *Snapshot) *c.SntRuntimeKeyIndexSnapshot {
    return @ptrCast(snapshot);
}

fn snapshotFromC(raw_snapshot: ?*c.SntRuntimeKeyIndexSnapshot) ?*Snapshot {
    const snapshot = raw_snapshot orelse return null;
    return @ptrCast(@alignCast(snapshot));
}

fn logCreateFailure(
    info: *const c.SntRuntimeKeyIndexCreateInfo,
    message: [*:0]const u8,
) void {
    if (info.log) |callback| {
        callback(info.user_data, c.SNT_ABI_LOG_ERROR, "runtime_key_index", message);
    }
}

fn logIndexFailure(index: *const Index, message: [*:0]const u8) void {
    if (index.create_info.log) |callback| {
        callback(index.create_info.user_data, c.SNT_ABI_LOG_ERROR, "runtime_key_index", message);
    }
}

pub export fn snt_runtime_key_index_create(
    create_info_input: ?*const c.SntRuntimeKeyIndexCreateInfo,
    out_index: ?*?*c.SntRuntimeKeyIndex,
) callconv(.c) Status {
    const output = out_index orelse return status_invalid_argument;
    output.* = null;
    const create_info = create_info_input orelse return status_invalid_argument;
    if (!validCreateInfo(create_info.*)) return status_invalid_argument;

    const allocator = std.heap.c_allocator;
    const index = allocator.create(Index) catch {
        logCreateFailure(create_info, "Runtime key index creation allocation failed");
        return status_internal_error;
    };
    errdefer allocator.destroy(index);

    const initial_snapshot = buildSnapshot(allocator, &[_]c.SntAbiByteView{}, 0) catch {
        logCreateFailure(create_info, "Runtime key index initial snapshot allocation failed");
        return status_internal_error;
    };

    index.* = .{
        .allocator = allocator,
        .create_info = create_info.*,
        .current_snapshot = initial_snapshot,
    };
    output.* = indexToC(index);
    return status_ok;
}

pub export fn snt_runtime_key_index_destroy(
    raw_index: ?*c.SntRuntimeKeyIndex,
) callconv(.c) void {
    const index = indexFromC(raw_index) orelse return;
    releaseSnapshot(index.current_snapshot);
    index.allocator.destroy(index);
}

pub export fn snt_runtime_key_index_rebuild(
    raw_index: ?*c.SntRuntimeKeyIndex,
    keys_input: ?*const c.SntAbiByteView,
    key_count_input: u64,
) callconv(.c) Status {
    const index = indexFromC(raw_index) orelse return status_invalid_argument;
    if (key_count_input > @as(u64, std.math.maxInt(c.SntRuntimeKeyId)) or
        key_count_input > @as(u64, @intCast(std.math.maxInt(usize))))
    {
        return status_invalid_argument;
    }
    const key_count: usize = @intCast(key_count_input);
    const input_keys: []const c.SntAbiByteView = if (key_count == 0)
        &[_]c.SntAbiByteView{}
    else
        @as([*]const c.SntAbiByteView, @ptrCast(keys_input orelse return status_invalid_argument))[0..key_count];
    if (index.current_snapshot.generation == std.math.maxInt(u64)) {
        return status_invalid_state;
    }

    const candidate = buildSnapshot(
        index.allocator,
        input_keys,
        index.current_snapshot.generation + 1,
    ) catch |err| switch (err) {
        error.InvalidArgument => return status_invalid_argument,
        error.OutOfMemory => {
            logIndexFailure(index, "Runtime key index rebuild allocation failed");
            return status_internal_error;
        },
    };

    const previous = index.current_snapshot;
    index.current_snapshot = candidate;
    releaseSnapshot(previous);
    return status_ok;
}

pub export fn snt_runtime_key_index_acquire_snapshot(
    raw_index: ?*const c.SntRuntimeKeyIndex,
    out_snapshot: ?*?*c.SntRuntimeKeyIndexSnapshot,
) callconv(.c) Status {
    const output = out_snapshot orelse return status_invalid_argument;
    output.* = null;
    const index = indexFromC(@constCast(raw_index)) orelse return status_invalid_argument;
    const snapshot = index.current_snapshot;
    if (!retainSnapshot(snapshot)) {
        logIndexFailure(index, "Runtime key index snapshot reference count exhausted");
        return status_internal_error;
    }
    output.* = snapshotToC(snapshot);
    return status_ok;
}

pub export fn snt_runtime_key_index_snapshot_retain(
    raw_snapshot: ?*c.SntRuntimeKeyIndexSnapshot,
) callconv(.c) Status {
    const snapshot = snapshotFromC(raw_snapshot) orelse return status_invalid_argument;
    return if (retainSnapshot(snapshot)) status_ok else status_internal_error;
}

pub export fn snt_runtime_key_index_snapshot_release(
    raw_snapshot: ?*c.SntRuntimeKeyIndexSnapshot,
) callconv(.c) void {
    const snapshot = snapshotFromC(raw_snapshot) orelse return;
    releaseSnapshot(snapshot);
}

pub export fn snt_runtime_key_index_restore_snapshot(
    raw_index: ?*c.SntRuntimeKeyIndex,
    raw_snapshot: ?*const c.SntRuntimeKeyIndexSnapshot,
) callconv(.c) Status {
    const index = indexFromC(raw_index) orelse return status_invalid_argument;
    const snapshot = snapshotFromC(@constCast(raw_snapshot)) orelse return status_invalid_argument;
    if (!retainSnapshot(snapshot)) {
        logIndexFailure(index, "Runtime key index snapshot reference count exhausted");
        return status_internal_error;
    }

    const previous = index.current_snapshot;
    index.current_snapshot = snapshot;
    releaseSnapshot(previous);
    return status_ok;
}

pub export fn snt_runtime_key_index_snapshot_query(
    raw_snapshot: ?*const c.SntRuntimeKeyIndexSnapshot,
    out_info: ?*c.SntRuntimeKeyIndexSnapshotInfo,
) callconv(.c) Status {
    const snapshot = snapshotFromC(@constCast(raw_snapshot)) orelse return status_invalid_argument;
    const output = out_info orelse return status_invalid_argument;
    if (!supportsStruct(c.SntRuntimeKeyIndexSnapshotInfo, output.struct_size)) {
        return status_invalid_argument;
    }
    output.reserved = 0;
    output.generation = snapshot.generation;
    output.key_count = @intCast(snapshot.keys.len);
    return status_ok;
}

pub export fn snt_runtime_key_index_snapshot_find_id(
    raw_snapshot: ?*const c.SntRuntimeKeyIndexSnapshot,
    key_view: c.SntAbiByteView,
    out_id: ?*c.SntRuntimeKeyId,
) callconv(.c) Status {
    const snapshot = snapshotFromC(@constCast(raw_snapshot)) orelse return status_invalid_argument;
    const output = out_id orelse return status_invalid_argument;
    const key = byteViewSlice(key_view) orelse return status_invalid_argument;
    output.* = c.SNT_RUNTIME_KEY_ID_INVALID;
    if (findKeyIndex(snapshot, key)) |index| {
        output.* = @intCast(index + 1);
    }
    return status_ok;
}

pub export fn snt_runtime_key_index_snapshot_find_key(
    raw_snapshot: ?*const c.SntRuntimeKeyIndexSnapshot,
    id: c.SntRuntimeKeyId,
    out_key: ?*c.SntAbiByteView,
) callconv(.c) Status {
    const snapshot = snapshotFromC(@constCast(raw_snapshot)) orelse return status_invalid_argument;
    const output = out_key orelse return status_invalid_argument;
    output.* = .{ .data = null, .size_bytes = 0 };
    if (id == c.SNT_RUNTIME_KEY_ID_INVALID) return status_ok;
    const key_index: usize = @intCast(id - 1);
    if (key_index >= snapshot.keys.len) return status_ok;

    const key = snapshot.keys[key_index].bytes;
    output.* = .{
        .data = key.ptr,
        .size_bytes = @intCast(key.len),
    };
    return status_ok;
}

test "rebuild publishes deterministic snapshots that outlive their index" {
    var create_info = std.mem.zeroes(c.SntRuntimeKeyIndexCreateInfo);
    create_info.struct_size = @sizeOf(c.SntRuntimeKeyIndexCreateInfo);
    var index: ?*c.SntRuntimeKeyIndex = null;
    try std.testing.expectEqual(
        status_ok,
        snt_runtime_key_index_create(&create_info, &index),
    );
    try std.testing.expect(index != null);

    var zinc = [_]u8{ 'z', 'i', 'n', 'c' };
    const charcoal = "charcoal";
    const iron = "iron";
    const keys = [_]c.SntAbiByteView{
        .{ .data = zinc[0..].ptr, .size_bytes = zinc.len },
        .{ .data = charcoal.ptr, .size_bytes = charcoal.len },
        .{ .data = iron.ptr, .size_bytes = iron.len },
    };
    try std.testing.expectEqual(
        status_ok,
        snt_runtime_key_index_rebuild(index, &keys[0], keys.len),
    );
    zinc[0] = 'X';

    var snapshot: ?*c.SntRuntimeKeyIndexSnapshot = null;
    try std.testing.expectEqual(
        status_ok,
        snt_runtime_key_index_acquire_snapshot(index, &snapshot),
    );
    try std.testing.expect(snapshot != null);
    try std.testing.expectEqual(
        status_ok,
        snt_runtime_key_index_snapshot_retain(snapshot),
    );

    const duplicate = [_]c.SntAbiByteView{
        .{ .data = iron.ptr, .size_bytes = iron.len },
        .{ .data = iron.ptr, .size_bytes = iron.len },
    };
    try std.testing.expectEqual(
        status_invalid_argument,
        snt_runtime_key_index_rebuild(index, &duplicate[0], duplicate.len),
    );
    snt_runtime_key_index_destroy(index);

    const zinc_key = "zinc";
    var zinc_id: c.SntRuntimeKeyId = c.SNT_RUNTIME_KEY_ID_INVALID;
    try std.testing.expectEqual(
        status_ok,
        snt_runtime_key_index_snapshot_find_id(
            snapshot,
            .{ .data = zinc_key.ptr, .size_bytes = zinc_key.len },
            &zinc_id,
        ),
    );
    try std.testing.expectEqual(@as(c.SntRuntimeKeyId, 3), zinc_id);

    var key_view = c.SntAbiByteView{ .data = null, .size_bytes = 0 };
    try std.testing.expectEqual(
        status_ok,
        snt_runtime_key_index_snapshot_find_key(snapshot, zinc_id, &key_view),
    );
    const key_length: usize = @intCast(key_view.size_bytes);
    try std.testing.expectEqualStrings("zinc", key_view.data[0..key_length]);
    snt_runtime_key_index_snapshot_release(snapshot);
    snt_runtime_key_index_snapshot_release(snapshot);
}

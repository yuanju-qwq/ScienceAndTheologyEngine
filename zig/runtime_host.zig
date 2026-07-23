// Zig-owned deterministic runtime host core.
//
// C++ or Zig sessions enter only through the C callback table. This module
// owns copied create inputs, ordered commands, published snapshot storage, and
// lease lifetime; it never receives C++ objects, allocators, or exceptions.

const std = @import("std");
const c = @cImport({
    @cInclude("abi/runtime_host_abi.h");
});

const Allocator = std.mem.Allocator;
const Status = c.SntAbiStatus;

const status_ok: Status = c.SNT_ABI_STATUS_OK;
const status_invalid_argument: Status = c.SNT_ABI_STATUS_INVALID_ARGUMENT;
const status_incompatible_version: Status = c.SNT_ABI_STATUS_INCOMPATIBLE_VERSION;
const status_unsupported: Status = c.SNT_ABI_STATUS_UNSUPPORTED;
const status_internal_error: Status = c.SNT_ABI_STATUS_INTERNAL_ERROR;
const status_not_ready: Status = c.SNT_ABI_STATUS_NOT_READY;
const status_invalid_state: Status = c.SNT_ABI_STATUS_INVALID_STATE;

const Phase = enum {
    initializing,
    idle,
    applying_commands,
    before_fixed_tick,
    running_fixed_systems,
    after_fixed_tick,
    shutdown,
};

const OwnedBytes = struct {
    bytes: ?[]u8 = null,

    fn fromView(allocator: Allocator, byte_view: c.SntAbiByteView) !OwnedBytes {
        if (byte_view.size_bytes == 0) return .{};

        const source = byte_view.data orelse return error.InvalidByteView;
        const byte_count: usize = @intCast(byte_view.size_bytes);
        const copied = try allocator.alloc(u8, byte_count);
        @memcpy(copied, source[0..byte_count]);
        return .{ .bytes = copied };
    }

    fn deinit(self: *OwnedBytes, allocator: Allocator) void {
        if (self.bytes) |bytes| allocator.free(bytes);
        self.bytes = null;
    }

    fn view(self: *const OwnedBytes) c.SntAbiByteView {
        if (self.bytes) |bytes| {
            return .{
                .data = @ptrCast(bytes.ptr),
                .size_bytes = @intCast(bytes.len),
            };
        }
        return .{ .data = null, .size_bytes = 0 };
    }
};

const CommandNode = struct {
    next: ?*CommandNode = null,
    command_type: u32,
    schema_version: u32,
    target_tick: u64,
    producer_high: u64,
    producer_low: u64,
    sequence: u64,
    payload: OwnedBytes,

    fn asAbiCommand(self: *const CommandNode) c.SntRuntimeCommand {
        return .{
            .struct_size = @intCast(@sizeOf(c.SntRuntimeCommand)),
            .command_type = self.command_type,
            .schema_version = self.schema_version,
            .flags = 0,
            .target_tick = self.target_tick,
            .producer_id = .{
                .high = self.producer_high,
                .low = self.producer_low,
            },
            .sequence = self.sequence,
            .payload = self.payload.view(),
        };
    }
};

const Snapshot = struct {
    ref_count: u64 = 1,
    schema_version: u32,
    simulation_tick: u64,
    presentation_sequence: u64,
    payload: OwnedBytes,
};

const LeaseNode = struct {
    next: ?*LeaseNode = null,
    lease_id: u64,
    snapshot: *Snapshot,
};

const Host = struct {
    allocator: Allocator,
    host_callbacks: c.SntRuntimeHostCallbacks,
    session_callbacks: c.SntRuntimeSessionCallbacks,
    paths: c.SntRuntimeHostPathRoots = undefined,
    runtime_config: c.SntRuntimeConfigBlob = undefined,
    session_config: c.SntRuntimeConfigBlob = undefined,
    engine_root: OwnedBytes = .{},
    game_root: OwnedBytes = .{},
    user_root: OwnedBytes = .{},
    runtime_config_payload: OwnedBytes = .{},
    session_config_payload: OwnedBytes = .{},
    fixed_tick_period_nanoseconds: u64,
    commands: ?*CommandNode = null,
    command_count: u64 = 0,
    latest_snapshot: ?*Snapshot = null,
    pending_snapshot: ?*Snapshot = null,
    leases: ?*LeaseNode = null,
    next_lease_id: u64 = 1,
    next_snapshot_sequence: u64 = 0,
    completed_tick: u64 = 0,
    executing_tick: u64 = 0,
    phase: Phase = .initializing,
    stop_requested: bool = false,
    session_initialized: bool = false,
};

fn supportsStruct(comptime T: type, supplied_size: anytype) bool {
    return @as(usize, @intCast(supplied_size)) >= @sizeOf(T);
}

fn validByteView(view: c.SntAbiByteView) bool {
    return (view.size_bytes == 0 or view.data != null) and
        view.size_bytes <= @as(u64, @intCast(std.math.maxInt(usize)));
}

fn validRequiredPath(view: c.SntAbiByteView) bool {
    return view.size_bytes != 0 and validByteView(view);
}

fn validConfigBlob(config: c.SntRuntimeConfigBlob) bool {
    return supportsStruct(c.SntRuntimeConfigBlob, config.struct_size) and
        validByteView(config.payload) and
        (config.schema_version != 0 or config.payload.size_bytes == 0);
}

fn validPathRoots(paths: c.SntRuntimeHostPathRoots) bool {
    return supportsStruct(c.SntRuntimeHostPathRoots, paths.struct_size) and
        paths.reserved == 0 and
        validRequiredPath(paths.engine_root_utf8) and
        validRequiredPath(paths.game_root_utf8) and
        validRequiredPath(paths.user_root_utf8);
}

fn validHostCallbacks(callbacks: c.SntRuntimeHostCallbacks) bool {
    return supportsStruct(c.SntRuntimeHostCallbacks, callbacks.struct_size) and
        callbacks.reserved == 0;
}

fn validSessionCallbacks(callbacks: c.SntRuntimeSessionCallbacks) bool {
    return supportsStruct(c.SntRuntimeSessionCallbacks, callbacks.struct_size) and
        callbacks.reserved == 0 and
        callbacks.initialize != null and
        callbacks.before_fixed_tick != null and
        callbacks.run_fixed_systems != null and
        callbacks.after_fixed_tick != null and
        callbacks.shutdown != null;
}

fn validCreateInfo(info: c.SntRuntimeHostCreateInfo) bool {
    return supportsStruct(c.SntRuntimeHostCreateInfo, info.struct_size) and
        info.flags == 0 and
        info.fixed_tick_period_nanoseconds != 0 and
        info.reserved[0] == 0 and info.reserved[1] == 0 and
        info.reserved[2] == 0 and info.reserved[3] == 0 and
        validPathRoots(info.paths) and
        validConfigBlob(info.runtime_config) and
        validConfigBlob(info.session_config) and
        validHostCallbacks(info.host_callbacks) and
        validSessionCallbacks(info.session_callbacks);
}

fn validCommand(command: c.SntRuntimeCommand) bool {
    return supportsStruct(c.SntRuntimeCommand, command.struct_size) and
        command.command_type != 0 and
        command.schema_version != 0 and
        command.flags == 0 and
        command.target_tick != 0 and
        validByteView(command.payload);
}

fn validSnapshotPublishInfo(info: c.SntRenderSnapshotPublishInfo) bool {
    return supportsStruct(c.SntRenderSnapshotPublishInfo, info.struct_size) and
        info.schema_version != 0 and validByteView(info.payload);
}

fn hostToC(host: *Host) *c.SntRuntimeHost {
    return @ptrCast(host);
}

fn hostFromC(raw_host: ?*c.SntRuntimeHost) ?*Host {
    const host = raw_host orelse return null;
    return @ptrCast(@alignCast(host));
}

fn logLifecycle(host: *Host, severity: c.SntAbiLogSeverity, message: [*:0]const u8) void {
    if (host.host_callbacks.log) |callback| {
        callback(host.host_callbacks.user_data, severity, "runtime_host", message);
    }
}

fn commandComesBefore(left: *const CommandNode, right: *const CommandNode) bool {
    if (left.target_tick != right.target_tick) return left.target_tick < right.target_tick;
    if (left.producer_high != right.producer_high) return left.producer_high < right.producer_high;
    if (left.producer_low != right.producer_low) return left.producer_low < right.producer_low;
    return left.sequence < right.sequence;
}

fn commandsHaveSameKey(left: *const CommandNode, right: *const CommandNode) bool {
    return left.target_tick == right.target_tick and
        left.producer_high == right.producer_high and
        left.producer_low == right.producer_low and
        left.sequence == right.sequence;
}

fn commandMatchesKey(node: *const CommandNode, command: c.SntRuntimeCommand) bool {
    return node.target_tick == command.target_tick and
        node.producer_high == command.producer_id.high and
        node.producer_low == command.producer_id.low and
        node.sequence == command.sequence;
}

fn queueContainsCommandKey(host: *const Host, command: c.SntRuntimeCommand) bool {
    var current = host.commands;
    while (current) |node| : (current = node.next) {
        if (commandMatchesKey(node, command)) return true;
    }
    return false;
}

fn insertCommand(host: *Host, node: *CommandNode) void {
    var previous: ?*CommandNode = null;
    var current = host.commands;
    while (current) |existing| {
        if (commandComesBefore(node, existing)) break;
        previous = existing;
        current = existing.next;
    }

    node.next = current;
    if (previous) |previous_node| {
        previous_node.next = node;
    } else {
        host.commands = node;
    }
}

fn destroyCommandNode(host: *Host, node: *CommandNode) void {
    node.payload.deinit(host.allocator);
    host.allocator.destroy(node);
}

fn clearCommands(host: *Host) void {
    var current = host.commands;
    host.commands = null;
    host.command_count = 0;
    while (current) |node| {
        current = node.next;
        destroyCommandNode(host, node);
    }
}

fn retainSnapshot(snapshot: *Snapshot) bool {
    if (snapshot.ref_count == std.math.maxInt(u64)) return false;
    snapshot.ref_count += 1;
    return true;
}

fn releaseSnapshot(host: *Host, snapshot: *Snapshot) void {
    std.debug.assert(snapshot.ref_count != 0);
    snapshot.ref_count -= 1;
    if (snapshot.ref_count == 0) {
        snapshot.payload.deinit(host.allocator);
        host.allocator.destroy(snapshot);
    }
}

fn discardPendingSnapshot(host: *Host) void {
    if (host.pending_snapshot) |snapshot| releaseSnapshot(host, snapshot);
    host.pending_snapshot = null;
}

fn commitPendingSnapshot(host: *Host) void {
    const pending = host.pending_snapshot orelse return;
    host.pending_snapshot = null;
    if (host.latest_snapshot) |previous| releaseSnapshot(host, previous);
    host.latest_snapshot = pending;
}

fn clearLeases(host: *Host) void {
    var current = host.leases;
    host.leases = null;
    while (current) |lease| {
        current = lease.next;
        releaseSnapshot(host, lease.snapshot);
        host.allocator.destroy(lease);
    }
}

fn latestSnapshotSequence(host: *const Host) u64 {
    if (host.latest_snapshot) |snapshot| return snapshot.presentation_sequence;
    return 0;
}

fn lifecycleState(host: *const Host) c.SntRuntimeHostLifecycleState {
    if (host.stop_requested) return c.SNT_RUNTIME_HOST_LIFECYCLE_STOP_REQUESTED;
    return c.SNT_RUNTIME_HOST_LIFECYCLE_RUNNING;
}

fn makeTickContext(host: *const Host, command_count: u64) c.SntRuntimeFixedTickContext {
    return .{
        .struct_size = @intCast(@sizeOf(c.SntRuntimeFixedTickContext)),
        .reserved = 0,
        .simulation_tick = host.executing_tick,
        .fixed_tick_period_nanoseconds = host.fixed_tick_period_nanoseconds,
        .command_count = command_count,
    };
}

fn countCommandsForTick(host: *const Host, tick: u64) u64 {
    var count: u64 = 0;
    var current = host.commands;
    while (current) |node| : (current = node.next) {
        if (node.target_tick != tick) break;
        count += 1;
    }
    return count;
}

fn writeState(host: *const Host, out_state: *c.SntRuntimeHostState) void {
    out_state.lifecycle_state = lifecycleState(host);
    out_state.completed_tick = host.completed_tick;
    out_state.queued_command_count = host.command_count;
    out_state.latest_snapshot_sequence = latestSnapshotSequence(host);
}

fn writeTickResult(host: *const Host, applied_commands: u64, out_result: *c.SntRuntimeFixedTickResult) void {
    out_result.lifecycle_state = lifecycleState(host);
    out_result.completed_tick = host.completed_tick;
    out_result.applied_command_count = applied_commands;
    out_result.latest_snapshot_sequence = latestSnapshotSequence(host);
}

fn callFixedCallback(host: *Host, callback: anytype, context: *const c.SntRuntimeFixedTickContext) Status {
    return callback(host.session_callbacks.user_data, hostToC(host), context);
}

fn failTick(host: *Host, status: Status, applied_commands: u64, out_result: *c.SntRuntimeFixedTickResult) Status {
    discardPendingSnapshot(host);
    host.phase = .idle;
    host.stop_requested = true;
    writeTickResult(host, applied_commands, out_result);
    logLifecycle(host, c.SNT_ABI_LOG_ERROR, "SntRuntimeHost stopped after a session callback failure");
    return status;
}

fn deinitHost(host: *Host) void {
    host.phase = .shutdown;
    if (host.session_initialized) {
        if (host.session_callbacks.shutdown) |callback| {
            callback(host.session_callbacks.user_data, hostToC(host));
        }
        host.session_initialized = false;
    }

    discardPendingSnapshot(host);
    if (host.latest_snapshot) |snapshot| releaseSnapshot(host, snapshot);
    host.latest_snapshot = null;
    clearLeases(host);
    clearCommands(host);
    host.engine_root.deinit(host.allocator);
    host.game_root.deinit(host.allocator);
    host.user_root.deinit(host.allocator);
    host.runtime_config_payload.deinit(host.allocator);
    host.session_config_payload.deinit(host.allocator);
    host.allocator.destroy(host);
}

pub export fn snt_runtime_host_create(
    create_info: ?*const c.SntRuntimeHostCreateInfo,
    out_host: ?*?*c.SntRuntimeHost,
) callconv(.c) Status {
    const output = out_host orelse return status_invalid_argument;
    output.* = null;
    const info = create_info orelse return status_invalid_argument;
    if (!validCreateInfo(info.*)) return status_invalid_argument;
    if (info.requested_abi_major != c.SNT_RUNTIME_ABI_MAJOR or
        info.requested_abi_minor > c.SNT_RUNTIME_ABI_MINOR)
    {
        return status_incompatible_version;
    }

    const allocator = std.heap.c_allocator;
    const host = allocator.create(Host) catch return status_internal_error;
    host.* = .{
        .allocator = allocator,
        .host_callbacks = info.host_callbacks,
        .session_callbacks = info.session_callbacks,
        .fixed_tick_period_nanoseconds = info.fixed_tick_period_nanoseconds,
    };
    errdefer deinitHost(host);

    host.engine_root = OwnedBytes.fromView(allocator, info.paths.engine_root_utf8) catch return status_internal_error;
    host.game_root = OwnedBytes.fromView(allocator, info.paths.game_root_utf8) catch return status_internal_error;
    host.user_root = OwnedBytes.fromView(allocator, info.paths.user_root_utf8) catch return status_internal_error;
    host.runtime_config_payload = OwnedBytes.fromView(allocator, info.runtime_config.payload) catch return status_internal_error;
    host.session_config_payload = OwnedBytes.fromView(allocator, info.session_config.payload) catch return status_internal_error;
    host.paths = .{
        .struct_size = @intCast(@sizeOf(c.SntRuntimeHostPathRoots)),
        .reserved = 0,
        .engine_root_utf8 = host.engine_root.view(),
        .game_root_utf8 = host.game_root.view(),
        .user_root_utf8 = host.user_root.view(),
    };
    host.runtime_config = .{
        .struct_size = @intCast(@sizeOf(c.SntRuntimeConfigBlob)),
        .schema_version = info.runtime_config.schema_version,
        .payload = host.runtime_config_payload.view(),
    };
    host.session_config = .{
        .struct_size = @intCast(@sizeOf(c.SntRuntimeConfigBlob)),
        .schema_version = info.session_config.schema_version,
        .payload = host.session_config_payload.view(),
    };

    const initialize = host.session_callbacks.initialize orelse return status_invalid_argument;
    const context = c.SntRuntimeSessionInitializeContext{
        .struct_size = @intCast(@sizeOf(c.SntRuntimeSessionInitializeContext)),
        .reserved = 0,
        .host = hostToC(host),
        .paths = &host.paths,
        .runtime_config = &host.runtime_config,
        .session_config = &host.session_config,
        .fixed_tick_period_nanoseconds = host.fixed_tick_period_nanoseconds,
    };
    const initialize_status = initialize(host.session_callbacks.user_data, &context);
    if (initialize_status != status_ok) return initialize_status;

    host.session_initialized = true;
    host.phase = .idle;
    logLifecycle(host, c.SNT_ABI_LOG_INFO, "SntRuntimeHost initialized with Zig deterministic core");
    output.* = hostToC(host);
    return status_ok;
}

pub export fn snt_runtime_host_query_state(
    raw_host: ?*const c.SntRuntimeHost,
    out_state: ?*c.SntRuntimeHostState,
) callconv(.c) Status {
    const host = hostFromC(@constCast(raw_host)) orelse return status_invalid_argument;
    const output = out_state orelse return status_invalid_argument;
    if (!supportsStruct(c.SntRuntimeHostState, output.struct_size)) return status_invalid_argument;
    writeState(host, output);
    return status_ok;
}

pub export fn snt_runtime_host_enqueue_command(
    raw_host: ?*c.SntRuntimeHost,
    command_input: ?*const c.SntRuntimeCommand,
) callconv(.c) Status {
    const host = hostFromC(raw_host) orelse return status_invalid_argument;
    const command = command_input orelse return status_invalid_argument;
    if (!validCommand(command.*)) return status_invalid_argument;
    if (host.session_callbacks.apply_command == null) return status_unsupported;

    const minimum_tick = if (host.phase == .idle or host.phase == .initializing)
        host.completed_tick + 1
    else
        host.executing_tick + 1;
    if (minimum_tick == 0 or command.target_tick < minimum_tick) return status_invalid_state;
    if (queueContainsCommandKey(host, command.*)) return status_invalid_argument;
    if (host.command_count == std.math.maxInt(u64)) return status_internal_error;

    const node = host.allocator.create(CommandNode) catch return status_internal_error;
    errdefer host.allocator.destroy(node);
    const payload = OwnedBytes.fromView(host.allocator, command.payload) catch return status_internal_error;
    node.* = .{
        .command_type = command.command_type,
        .schema_version = command.schema_version,
        .target_tick = command.target_tick,
        .producer_high = command.producer_id.high,
        .producer_low = command.producer_id.low,
        .sequence = command.sequence,
        .payload = payload,
    };
    insertCommand(host, node);
    host.command_count += 1;
    return status_ok;
}

pub export fn snt_runtime_host_run_fixed_tick(
    raw_host: ?*c.SntRuntimeHost,
    expected_tick: u64,
    out_result: ?*c.SntRuntimeFixedTickResult,
) callconv(.c) Status {
    const host = hostFromC(raw_host) orelse return status_invalid_argument;
    const output = out_result orelse return status_invalid_argument;
    if (expected_tick == 0 or !supportsStruct(c.SntRuntimeFixedTickResult, output.struct_size)) {
        return status_invalid_argument;
    }
    if (host.phase != .idle) return status_invalid_state;
    if (host.stop_requested) {
        writeTickResult(host, 0, output);
        return status_not_ready;
    }
    if (host.completed_tick == std.math.maxInt(u64) or expected_tick != host.completed_tick + 1) {
        return status_invalid_state;
    }

    host.executing_tick = expected_tick;
    const scheduled_command_count = countCommandsForTick(host, expected_tick);
    const context = makeTickContext(host, scheduled_command_count);
    var applied_commands: u64 = 0;

    host.phase = .applying_commands;
    while (host.commands) |node| {
        if (node.target_tick != expected_tick) break;
        host.commands = node.next;
        host.command_count -= 1;
        const command = node.asAbiCommand();
        const callback = host.session_callbacks.apply_command orelse {
            destroyCommandNode(host, node);
            return failTick(host, status_unsupported, applied_commands, output);
        };
        const status = callback(host.session_callbacks.user_data, hostToC(host), &context, &command);
        destroyCommandNode(host, node);
        if (status != status_ok) return failTick(host, status, applied_commands, output);
        applied_commands += 1;
    }

    host.phase = .before_fixed_tick;
    const before = host.session_callbacks.before_fixed_tick orelse return failTick(host, status_internal_error, applied_commands, output);
    const before_status = callFixedCallback(host, before, &context);
    if (before_status != status_ok) return failTick(host, before_status, applied_commands, output);

    host.phase = .running_fixed_systems;
    const systems = host.session_callbacks.run_fixed_systems orelse return failTick(host, status_internal_error, applied_commands, output);
    const systems_status = callFixedCallback(host, systems, &context);
    if (systems_status != status_ok) return failTick(host, systems_status, applied_commands, output);

    host.phase = .after_fixed_tick;
    const after = host.session_callbacks.after_fixed_tick orelse return failTick(host, status_internal_error, applied_commands, output);
    const after_status = callFixedCallback(host, after, &context);
    if (after_status != status_ok) return failTick(host, after_status, applied_commands, output);

    commitPendingSnapshot(host);
    host.completed_tick = expected_tick;
    host.executing_tick = 0;
    host.phase = .idle;
    writeTickResult(host, applied_commands, output);
    return status_ok;
}

pub export fn snt_runtime_host_publish_render_snapshot(
    raw_host: ?*c.SntRuntimeHost,
    publish_info_input: ?*const c.SntRenderSnapshotPublishInfo,
) callconv(.c) Status {
    const host = hostFromC(raw_host) orelse return status_invalid_argument;
    const publish_info = publish_info_input orelse return status_invalid_argument;
    if (!validSnapshotPublishInfo(publish_info.*)) return status_invalid_argument;
    if (host.phase != .after_fixed_tick) return status_invalid_state;
    if (host.next_snapshot_sequence == std.math.maxInt(u64)) return status_internal_error;

    const payload = OwnedBytes.fromView(host.allocator, publish_info.payload) catch return status_internal_error;
    const snapshot = host.allocator.create(Snapshot) catch {
        var mutable_payload = payload;
        mutable_payload.deinit(host.allocator);
        return status_internal_error;
    };
    host.next_snapshot_sequence += 1;
    snapshot.* = .{
        .schema_version = publish_info.schema_version,
        .simulation_tick = host.executing_tick,
        .presentation_sequence = host.next_snapshot_sequence,
        .payload = payload,
    };
    discardPendingSnapshot(host);
    host.pending_snapshot = snapshot;
    return status_ok;
}

pub export fn snt_runtime_host_acquire_render_snapshot(
    raw_host: ?*c.SntRuntimeHost,
    out_lease: ?*c.SntRenderSnapshotLease,
) callconv(.c) Status {
    const host = hostFromC(raw_host) orelse return status_invalid_argument;
    const output = out_lease orelse return status_invalid_argument;
    if (!supportsStruct(c.SntRenderSnapshotLease, output.struct_size)) return status_invalid_argument;
    const snapshot = host.latest_snapshot orelse return status_not_ready;
    if (host.next_lease_id == 0 or host.next_lease_id == std.math.maxInt(u64)) return status_internal_error;
    if (!retainSnapshot(snapshot)) return status_internal_error;

    const lease = host.allocator.create(LeaseNode) catch {
        releaseSnapshot(host, snapshot);
        return status_internal_error;
    };
    const lease_id = host.next_lease_id;
    host.next_lease_id += 1;
    lease.* = .{
        .lease_id = lease_id,
        .snapshot = snapshot,
        .next = host.leases,
    };
    host.leases = lease;
    output.lease_id = lease_id;
    output.reserved = 0;
    output.snapshot = .{
        .struct_size = @intCast(@sizeOf(c.SntRenderSnapshotView)),
        .schema_version = snapshot.schema_version,
        .simulation_tick = snapshot.simulation_tick,
        .presentation_sequence = snapshot.presentation_sequence,
        .payload = snapshot.payload.view(),
    };
    return status_ok;
}

pub export fn snt_runtime_host_release_render_snapshot(
    raw_host: ?*c.SntRuntimeHost,
    lease_id: c.SntRenderSnapshotLeaseId,
) callconv(.c) Status {
    const host = hostFromC(raw_host) orelse return status_invalid_argument;
    if (lease_id == c.SNT_RENDER_SNAPSHOT_LEASE_INVALID) return status_invalid_argument;

    var previous: ?*LeaseNode = null;
    var current = host.leases;
    while (current) |lease| {
        if (lease.lease_id == lease_id) {
            if (previous) |previous_lease| {
                previous_lease.next = lease.next;
            } else {
                host.leases = lease.next;
            }
            releaseSnapshot(host, lease.snapshot);
            host.allocator.destroy(lease);
            return status_ok;
        }
        previous = lease;
        current = lease.next;
    }
    return status_invalid_argument;
}

pub export fn snt_runtime_host_request_stop(
    raw_host: ?*c.SntRuntimeHost,
) callconv(.c) Status {
    const host = hostFromC(raw_host) orelse return status_invalid_argument;
    if (host.phase == .shutdown) return status_invalid_state;
    host.stop_requested = true;
    return status_ok;
}

pub export fn snt_runtime_host_shutdown(raw_host: ?*c.SntRuntimeHost) callconv(.c) void {
    const host = hostFromC(raw_host) orelse return;
    if (host.phase != .idle and host.phase != .initializing) {
        host.stop_requested = true;
        return;
    }
    logLifecycle(host, c.SNT_ABI_LOG_INFO, "SntRuntimeHost shutting down Zig deterministic core");
    deinitHost(host);
}

test "command ordering is target then producer then sequence" {
    var first = CommandNode{
        .command_type = 1,
        .schema_version = 1,
        .target_tick = 2,
        .producer_high = 0,
        .producer_low = 1,
        .sequence = 1,
        .payload = .{},
    };
    var second = CommandNode{
        .command_type = 1,
        .schema_version = 1,
        .target_tick = 2,
        .producer_high = 1,
        .producer_low = 0,
        .sequence = 0,
        .payload = .{},
    };
    var third = CommandNode{
        .command_type = 1,
        .schema_version = 1,
        .target_tick = 3,
        .producer_high = 0,
        .producer_low = 0,
        .sequence = 0,
        .payload = .{},
    };

    try std.testing.expect(commandComesBefore(&first, &second));
    try std.testing.expect(commandComesBefore(&second, &third));
    try std.testing.expect(!commandsHaveSameKey(&first, &second));
    second.sequence = first.sequence;
    second.producer_high = first.producer_high;
    second.producer_low = first.producer_low;
    try std.testing.expect(commandsHaveSameKey(&first, &second));
}

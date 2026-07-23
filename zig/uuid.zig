// Zig-owned UUID generator state machine.
//
// Platform-specific code supplies entropy values through the C ABI. This
// module owns the deterministic seed mixer and the counter-to-UUID mapping;
// it allocates no memory and can be embedded by C++, C, or Zig callers.

const std = @import("std");
const c = @cImport({
    @cInclude("abi/uuid_abi.h");
});

const Status = c.SntAbiStatus;
const status_ok: Status = c.SNT_ABI_STATUS_OK;
const status_invalid_argument: Status = c.SNT_ABI_STATUS_INVALID_ARGUMENT;
const status_invalid_state: Status = c.SNT_ABI_STATUS_INVALID_STATE;

fn supportsStruct(comptime T: type, supplied_size: anytype) bool {
    return @as(usize, @intCast(supplied_size)) >= @sizeOf(T);
}

fn hasZeroReservedEntropyFields(entropy: c.SntUuidGeneratorEntropy) bool {
    return entropy.reserved == 0 and
        entropy.reserved_words[0] == 0 and entropy.reserved_words[1] == 0;
}

fn hasZeroReservedStateFields(state: c.SntUuidGeneratorState) bool {
    return state.reserved == 0 and
        state.reserved_words[0] == 0 and state.reserved_words[1] == 0 and
        state.reserved_words[2] == 0;
}

fn validEntropy(entropy: c.SntUuidGeneratorEntropy) bool {
    return supportsStruct(c.SntUuidGeneratorEntropy, entropy.struct_size) and
        hasZeroReservedEntropyFields(entropy);
}

fn validInitializedState(state: c.SntUuidGeneratorState) bool {
    return supportsStruct(c.SntUuidGeneratorState, state.struct_size) and
        hasZeroReservedStateFields(state) and
        (state.seed_high != 0 or state.seed_low != 0);
}

fn mixSeed(a: u32, b: u32) u32 {
    var mixed = a ^ (b +% 0x9e3779b9 +% (a << 6) +% (a >> 2));
    mixed ^= mixed >> 17;
    mixed *%= 0xed5ad4bb;
    mixed ^= mixed >> 11;
    mixed *%= 0xac4c1b51;
    mixed ^= mixed >> 15;
    mixed *%= 0x31848bab;
    mixed ^= mixed >> 14;
    return mixed;
}

fn initialState(entropy: c.SntUuidGeneratorEntropy) c.SntUuidGeneratorState {
    const now_low: u32 = @truncate(entropy.steady_clock_ticks);
    const now_high: u32 = @truncate(entropy.steady_clock_ticks >> 32);
    var seed_high = (@as(u64, mixSeed(now_high, entropy.random_words[0])) << 32) |
        @as(u64, mixSeed(now_low, entropy.random_words[1]));
    const seed_low = (@as(u64, mixSeed(entropy.random_words[2], now_low)) << 32) |
        @as(u64, mixSeed(entropy.random_words[3], now_high));
    if (seed_high == 0 and seed_low == 0) seed_high = 1;

    return .{
        .struct_size = @sizeOf(c.SntUuidGeneratorState),
        .reserved = 0,
        .seed_high = seed_high,
        .seed_low = seed_low,
        .counter = 0,
        .reserved_words = .{ 0, 0, 0 },
    };
}

fn pack(state: *const c.SntUuidGeneratorState, counter: u64) c.SntUuid {
    return .{
        .low = state.seed_low ^ counter,
        .high = state.seed_high,
    };
}

fn nextValidCounter(state: *const c.SntUuidGeneratorState) u64 {
    var counter = state.counter +% 1;
    // The zero seed pair is rejected during initialization, so at most one
    // counter can map to the reserved invalid UUID. Skip it without exposing
    // an invalid value through next or peek after reset/wrap.
    if (state.seed_high == 0 and counter == state.seed_low) {
        counter +%= 1;
    }
    return counter;
}

pub export fn snt_uuid_generator_initialize(
    state_input: ?*c.SntUuidGeneratorState,
    entropy_input: ?*const c.SntUuidGeneratorEntropy,
) callconv(.c) Status {
    const state = state_input orelse return status_invalid_argument;
    const entropy = entropy_input orelse return status_invalid_argument;
    if (!supportsStruct(c.SntUuidGeneratorState, state.struct_size) or
        !hasZeroReservedStateFields(state.*) or !validEntropy(entropy.*))
    {
        return status_invalid_argument;
    }

    state.* = initialState(entropy.*);
    return status_ok;
}

pub export fn snt_uuid_generator_next(
    state_input: ?*c.SntUuidGeneratorState,
    out_uuid: ?*c.SntUuid,
) callconv(.c) Status {
    const state = state_input orelse return status_invalid_argument;
    const output = out_uuid orelse return status_invalid_argument;
    if (!validInitializedState(state.*)) return status_invalid_state;

    state.counter = nextValidCounter(state);
    output.* = pack(state, state.counter);
    return status_ok;
}

pub export fn snt_uuid_generator_peek_next(
    state_input: ?*const c.SntUuidGeneratorState,
    out_uuid: ?*c.SntUuid,
) callconv(.c) Status {
    const state = state_input orelse return status_invalid_argument;
    const output = out_uuid orelse return status_invalid_argument;
    if (!validInitializedState(state.*)) return status_invalid_state;

    output.* = pack(state, nextValidCounter(state));
    return status_ok;
}

pub export fn snt_uuid_generator_reset_counter(
    state_input: ?*c.SntUuidGeneratorState,
    counter: u64,
) callconv(.c) Status {
    const state = state_input orelse return status_invalid_argument;
    if (!validInitializedState(state.*)) return status_invalid_state;
    state.counter = counter;
    return status_ok;
}

test "identical entropy produces identical UUID sequences" {
    var entropy = std.mem.zeroes(c.SntUuidGeneratorEntropy);
    entropy.struct_size = @sizeOf(c.SntUuidGeneratorEntropy);
    entropy.steady_clock_ticks = 0x1122334455667788;
    entropy.random_words = .{ 1, 2, 3, 4 };

    var first_state = std.mem.zeroes(c.SntUuidGeneratorState);
    first_state.struct_size = @sizeOf(c.SntUuidGeneratorState);
    var second_state = std.mem.zeroes(c.SntUuidGeneratorState);
    second_state.struct_size = @sizeOf(c.SntUuidGeneratorState);
    try std.testing.expectEqual(status_ok, snt_uuid_generator_initialize(&first_state, &entropy));
    try std.testing.expectEqual(status_ok, snt_uuid_generator_initialize(&second_state, &entropy));

    var first_uuid = c.SntUuid{ .low = 0, .high = 0 };
    var second_uuid = c.SntUuid{ .low = 0, .high = 0 };
    try std.testing.expectEqual(status_ok, snt_uuid_generator_next(&first_state, &first_uuid));
    try std.testing.expectEqual(status_ok, snt_uuid_generator_next(&second_state, &second_uuid));
    try std.testing.expectEqual(first_uuid.low, second_uuid.low);
    try std.testing.expectEqual(first_uuid.high, second_uuid.high);
    try std.testing.expect(first_uuid.low != 0 or first_uuid.high != 0);

    var peeked_uuid = c.SntUuid{ .low = 0, .high = 0 };
    try std.testing.expectEqual(status_ok, snt_uuid_generator_peek_next(&first_state, &peeked_uuid));
    try std.testing.expectEqual(status_ok, snt_uuid_generator_next(&first_state, &first_uuid));
    try std.testing.expectEqual(peeked_uuid.low, first_uuid.low);
    try std.testing.expectEqual(peeked_uuid.high, first_uuid.high);
}

test "state rejects use before Zig initialization" {
    var state = std.mem.zeroes(c.SntUuidGeneratorState);
    state.struct_size = @sizeOf(c.SntUuidGeneratorState);
    var output = c.SntUuid{ .low = 7, .high = 9 };
    try std.testing.expectEqual(status_invalid_state, snt_uuid_generator_next(&state, &output));
    try std.testing.expectEqual(@as(u64, 7), output.low);
    try std.testing.expectEqual(@as(u64, 9), output.high);
}

test "generator skips the invalid UUID sentinel after reset" {
    var state = std.mem.zeroes(c.SntUuidGeneratorState);
    state.struct_size = @sizeOf(c.SntUuidGeneratorState);
    state.seed_low = 1;
    state.counter = 0;

    var peeked = c.SntUuid{ .low = 0, .high = 0 };
    var issued = c.SntUuid{ .low = 0, .high = 0 };
    try std.testing.expectEqual(status_ok, snt_uuid_generator_reset_counter(&state, 0));
    try std.testing.expectEqual(status_ok, snt_uuid_generator_peek_next(&state, &peeked));
    try std.testing.expectEqual(status_ok, snt_uuid_generator_next(&state, &issued));
    try std.testing.expectEqual(@as(u64, 3), peeked.low);
    try std.testing.expectEqual(@as(u64, 0), peeked.high);
    try std.testing.expectEqual(peeked.low, issued.low);
    try std.testing.expectEqual(peeked.high, issued.high);
    try std.testing.expectEqual(@as(u64, 2), state.counter);
}

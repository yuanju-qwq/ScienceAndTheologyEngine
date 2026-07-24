// Zig-owned, immutable JSON document adapter.
//
// The C ABI exposes opaque document/value handles and borrowed UTF-8 views.
// std.json remains private to this module, while callers use only project ABI
// types. Parsed strings are always copied into the document arena so callers
// may release or mutate the original source after parse returns.

const std = @import("std");
const c = @cImport({
    @cInclude("abi/json_abi.h");
});

const Allocator = std.mem.Allocator;
const Status = c.SntAbiStatus;
const Value = std.json.Value;
const ParsedValue = std.json.Parsed(Value);

const allocator: Allocator = std.heap.c_allocator;
const status_ok: Status = c.SNT_ABI_STATUS_OK;
const status_invalid_argument: Status = c.SNT_ABI_STATUS_INVALID_ARGUMENT;
const status_internal_error: Status = c.SNT_ABI_STATUS_INTERNAL_ERROR;

const Document = struct {
    parsed: ParsedValue,
};

fn byteViewSlice(view: c.SntAbiByteView) ?[]const u8 {
    if (view.size_bytes > @as(u64, @intCast(std.math.maxInt(usize)))) return null;
    const byte_count: usize = @intCast(view.size_bytes);
    if (byte_count == 0) return &[_]u8{};
    const data = view.data orelse return null;
    return data[0..byte_count];
}

fn documentToC(document: *Document) *c.SntJsonDocument {
    return @ptrCast(document);
}

fn documentFromC(raw_document: ?*c.SntJsonDocument) ?*Document {
    const document = raw_document orelse return null;
    return @ptrCast(@alignCast(document));
}

fn documentFromCConst(raw_document: ?*const c.SntJsonDocument) ?*const Document {
    const document = raw_document orelse return null;
    return @ptrCast(@alignCast(document));
}

fn valueToC(value: *const Value) *const c.SntJsonValue {
    return @ptrCast(value);
}

fn valueFromC(raw_value: ?*const c.SntJsonValue) ?*const Value {
    const value = raw_value orelse return null;
    return @ptrCast(@alignCast(value));
}

fn valueType(value: Value) c.SntJsonValueType {
    return switch (value) {
        .null => c.SNT_JSON_VALUE_TYPE_NULL,
        .bool => c.SNT_JSON_VALUE_TYPE_BOOL,
        .integer => c.SNT_JSON_VALUE_TYPE_INTEGER,
        .float => c.SNT_JSON_VALUE_TYPE_FLOAT,
        .number_string => c.SNT_JSON_VALUE_TYPE_NUMBER_STRING,
        .string => c.SNT_JSON_VALUE_TYPE_STRING,
        .array => c.SNT_JSON_VALUE_TYPE_ARRAY,
        .object => c.SNT_JSON_VALUE_TYPE_OBJECT,
    };
}

pub export fn snt_json_document_parse(
    json_utf8: c.SntAbiByteView,
    out_document: ?*?*c.SntJsonDocument,
) callconv(.c) Status {
    const output = out_document orelse return status_invalid_argument;
    output.* = null;
    const source = byteViewSlice(json_utf8) orelse return status_invalid_argument;

    const document = allocator.create(Document) catch return status_internal_error;
    errdefer allocator.destroy(document);
    const parsed = std.json.parseFromSlice(Value, allocator, source, .{
        .allocate = .alloc_always,
    }) catch |err| switch (err) {
        error.OutOfMemory => return status_internal_error,
        else => return status_invalid_argument,
    };
    document.* = .{ .parsed = parsed };
    output.* = documentToC(document);
    return status_ok;
}

pub export fn snt_json_document_destroy(
    document_input: ?*c.SntJsonDocument,
) callconv(.c) void {
    const document = documentFromC(document_input) orelse return;
    document.parsed.deinit();
    allocator.destroy(document);
}

pub export fn snt_json_document_root(
    document_input: ?*const c.SntJsonDocument,
    out_root: ?*?*const c.SntJsonValue,
) callconv(.c) Status {
    const document = documentFromCConst(document_input) orelse return status_invalid_argument;
    const output = out_root orelse return status_invalid_argument;
    output.* = valueToC(&document.parsed.value);
    return status_ok;
}

pub export fn snt_json_value_query_type(
    value_input: ?*const c.SntJsonValue,
    out_type: ?*c.SntJsonValueType,
) callconv(.c) Status {
    const value = valueFromC(value_input) orelse return status_invalid_argument;
    const output = out_type orelse return status_invalid_argument;
    output.* = valueType(value.*);
    return status_ok;
}

pub export fn snt_json_value_read_bool(
    value_input: ?*const c.SntJsonValue,
    out_bool: ?*u32,
) callconv(.c) Status {
    const value = valueFromC(value_input) orelse return status_invalid_argument;
    const output = out_bool orelse return status_invalid_argument;
    switch (value.*) {
        .bool => |boolean| output.* = @intFromBool(boolean),
        else => return status_invalid_argument,
    }
    return status_ok;
}

pub export fn snt_json_value_read_int64(
    value_input: ?*const c.SntJsonValue,
    out_value: ?*i64,
) callconv(.c) Status {
    const value = valueFromC(value_input) orelse return status_invalid_argument;
    const output = out_value orelse return status_invalid_argument;
    switch (value.*) {
        .integer => |integer| output.* = integer,
        else => return status_invalid_argument,
    }
    return status_ok;
}

pub export fn snt_json_value_read_uint64(
    value_input: ?*const c.SntJsonValue,
    out_value: ?*u64,
) callconv(.c) Status {
    const value = valueFromC(value_input) orelse return status_invalid_argument;
    const output = out_value orelse return status_invalid_argument;
    switch (value.*) {
        .integer => |integer| {
            if (integer < 0) return status_invalid_argument;
            output.* = @intCast(integer);
        },
        else => return status_invalid_argument,
    }
    return status_ok;
}

pub export fn snt_json_value_read_float64(
    value_input: ?*const c.SntJsonValue,
    out_value: ?*f64,
) callconv(.c) Status {
    const value = valueFromC(value_input) orelse return status_invalid_argument;
    const output = out_value orelse return status_invalid_argument;
    switch (value.*) {
        .integer => |integer| output.* = @floatFromInt(integer),
        .float => |float| output.* = float,
        else => return status_invalid_argument,
    }
    return status_ok;
}

pub export fn snt_json_value_read_string(
    value_input: ?*const c.SntJsonValue,
    out_utf8: ?*c.SntAbiByteView,
) callconv(.c) Status {
    const value = valueFromC(value_input) orelse return status_invalid_argument;
    const output = out_utf8 orelse return status_invalid_argument;
    output.* = .{ .data = null, .size_bytes = 0 };
    switch (value.*) {
        .string => |text| {
            if (text.len != 0) {
                output.* = .{
                    .data = @ptrCast(text.ptr),
                    .size_bytes = @intCast(text.len),
                };
            }
        },
        else => return status_invalid_argument,
    }
    return status_ok;
}

pub export fn snt_json_object_find(
    object_input: ?*const c.SntJsonValue,
    key_utf8: c.SntAbiByteView,
    out_value: ?*?*const c.SntJsonValue,
) callconv(.c) Status {
    const object = valueFromC(object_input) orelse return status_invalid_argument;
    const key = byteViewSlice(key_utf8) orelse return status_invalid_argument;
    const output = out_value orelse return status_invalid_argument;
    output.* = null;
    switch (object.*) {
        .object => |*map| {
            if (map.getPtr(key)) |value| output.* = valueToC(value);
        },
        else => return status_invalid_argument,
    }
    return status_ok;
}

pub export fn snt_json_array_count(
    array_input: ?*const c.SntJsonValue,
    out_count: ?*u64,
) callconv(.c) Status {
    const array = valueFromC(array_input) orelse return status_invalid_argument;
    const output = out_count orelse return status_invalid_argument;
    switch (array.*) {
        .array => |items| output.* = @intCast(items.items.len),
        else => return status_invalid_argument,
    }
    return status_ok;
}

pub export fn snt_json_array_get(
    array_input: ?*const c.SntJsonValue,
    index: u64,
    out_element: ?*?*const c.SntJsonValue,
) callconv(.c) Status {
    const array = valueFromC(array_input) orelse return status_invalid_argument;
    const output = out_element orelse return status_invalid_argument;
    output.* = null;
    switch (array.*) {
        .array => |*items| {
            if (index >= items.items.len) return status_invalid_argument;
            output.* = valueToC(&items.items[@intCast(index)]);
        },
        else => return status_invalid_argument,
    }
    return status_ok;
}

test "document owns parsed values and supports typed read-only queries" {
    var source = [_]u8{ '{', '"', 'n', 'a', 'm', 'e', '"', ':', '"', 'z', 'i', 'g', '"', ',', '"', 'v', 'a', 'l', 'u', 'e', '"', ':', '7', ',', '"', 'f', 'l', 'a', 'g', '"', ':', 't', 'r', 'u', 'e', ',', '"', 'i', 't', 'e', 'm', 's', '"', ':', '[', '"', 'a', '"', ',', '"', 'b', '"', ']', '}' };
    var document: ?*c.SntJsonDocument = null;
    const input = c.SntAbiByteView{ .data = &source, .size_bytes = source.len };
    try std.testing.expectEqual(status_ok, snt_json_document_parse(input, &document));
    defer snt_json_document_destroy(document);
    source[10] = 'X';

    var root: ?*const c.SntJsonValue = null;
    try std.testing.expectEqual(status_ok, snt_json_document_root(document, &root));
    var name: ?*const c.SntJsonValue = null;
    const name_key = "name";
    try std.testing.expectEqual(status_ok, snt_json_object_find(root, .{ .data = name_key.ptr, .size_bytes = name_key.len }, &name));
    var name_text = c.SntAbiByteView{ .data = null, .size_bytes = 0 };
    try std.testing.expectEqual(status_ok, snt_json_value_read_string(name, &name_text));
    const name_bytes: [*]const u8 = @ptrCast(name_text.data);
    try std.testing.expectEqualStrings("zig", name_bytes[0..@intCast(name_text.size_bytes)]);

    var value: ?*const c.SntJsonValue = null;
    const value_key = "value";
    try std.testing.expectEqual(status_ok, snt_json_object_find(root, .{ .data = value_key.ptr, .size_bytes = value_key.len }, &value));
    var number: u64 = 0;
    try std.testing.expectEqual(status_ok, snt_json_value_read_uint64(value, &number));
    try std.testing.expectEqual(@as(u64, 7), number);

    var missing: ?*const c.SntJsonValue = undefined;
    const missing_key = "missing";
    try std.testing.expectEqual(status_ok, snt_json_object_find(root, .{ .data = missing_key.ptr, .size_bytes = missing_key.len }, &missing));
    try std.testing.expect(missing == null);
}

test "parser rejects malformed inputs and duplicate keys" {
    var document: ?*c.SntJsonDocument = @ptrFromInt(1);
    const malformed = "{ invalid json";
    try std.testing.expectEqual(
        status_invalid_argument,
        snt_json_document_parse(.{ .data = malformed.ptr, .size_bytes = malformed.len }, &document),
    );
    try std.testing.expect(document == null);

    const duplicate = "{\"key\":1,\"key\":2}";
    try std.testing.expectEqual(
        status_invalid_argument,
        snt_json_document_parse(.{ .data = duplicate.ptr, .size_bytes = duplicate.len }, &document),
    );
    try std.testing.expect(document == null);
}

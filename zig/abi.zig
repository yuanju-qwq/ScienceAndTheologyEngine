// Single Zig archive root for the language-neutral native ABI.
//
// Leaf modules keep their own focused tests, while this root makes every
// exported C symbol part of the one snt_abi static archive.

comptime {
    _ = @import("hash.zig");
    _ = @import("json.zig");
    _ = @import("runtime_host.zig");
    _ = @import("runtime_key_index.zig");
    _ = @import("uuid.zig");
}

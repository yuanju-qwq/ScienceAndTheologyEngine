// Unit tests for BinaryWriter / BinaryReader / Serializer<T>.
//
// Phase 3: validates the binary I/O layer that the scene system will
// build on. Covers:
//   - Round-trip: write -> read -> compare for every primitive type.
//   - Bounds checking: truncated reads return false.
//   - String length-prefix + truncation / oversize length detection.
//   - Component Serializer<T> specializations: Transform, MeshRef,
//     Camera, EntityGuid round-trip.
//   - Multi-field struct writes preserve field order.

#include "core/binary_reader.h"
#include "core/binary_writer.h"
#include "core/serializer.h"
#include "ecs/components.h"
#include "ecs/entity_guid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using snt::core::BinaryReader;
using snt::core::BinaryWriter;
using snt::core::Serializer;
using snt::ecs::Camera;
using snt::ecs::EntityGuid;
using snt::ecs::kInvalidEntityGuid;
using snt::ecs::MeshHandle;
using snt::ecs::MeshRef;
using snt::ecs::Transform;

// ===========================================================================
// BinaryWriter / BinaryReader primitives
// ===========================================================================

TEST(BinaryIOTest, WriteReadU8) {
    BinaryWriter w;
    w.write_u8(0xAB);
    BinaryReader r{w.buffer()};
    uint8_t v = 0;
    ASSERT_TRUE(r.read_u8(v));
    EXPECT_EQ(v, 0xABu);
    EXPECT_TRUE(r.eof());
}

TEST(BinaryIOTest, WriteReadU16) {
    BinaryWriter w;
    w.write_u16(0x1234);
    BinaryReader r{w.buffer()};
    uint16_t v = 0;
    ASSERT_TRUE(r.read_u16(v));
    EXPECT_EQ(v, 0x1234u);
    EXPECT_TRUE(r.eof());
}

TEST(BinaryIOTest, WriteReadU32) {
    BinaryWriter w;
    w.write_u32(0xDEADBEEFu);
    BinaryReader r{w.buffer()};
    uint32_t v = 0;
    ASSERT_TRUE(r.read_u32(v));
    EXPECT_EQ(v, 0xDEADBEEFu);
    EXPECT_TRUE(r.eof());
}

TEST(BinaryIOTest, WriteReadU64) {
    BinaryWriter w;
    w.write_u64(0x0123456789ABCDEFull);
    BinaryReader r{w.buffer()};
    uint64_t v = 0;
    ASSERT_TRUE(r.read_u64(v));
    EXPECT_EQ(v, 0x0123456789ABCDEFull);
    EXPECT_TRUE(r.eof());
}

TEST(BinaryIOTest, WriteReadI32Negative) {
    BinaryWriter w;
    w.write_i32(-42);
    BinaryReader r{w.buffer()};
    int32_t v = 0;
    ASSERT_TRUE(r.read_i32(v));
    EXPECT_EQ(v, -42);
}

TEST(BinaryIOTest, WriteReadF32) {
    BinaryWriter w;
    w.write_f32(3.14159f);
    BinaryReader r{w.buffer()};
    float v = 0.0f;
    ASSERT_TRUE(r.read_f32(v));
    EXPECT_FLOAT_EQ(v, 3.14159f);
}

TEST(BinaryIOTest, WriteReadF64) {
    BinaryWriter w;
    w.write_f64(2.718281828459045);
    BinaryReader r{w.buffer()};
    double v = 0.0;
    ASSERT_TRUE(r.read_f64(v));
    EXPECT_DOUBLE_EQ(v, 2.718281828459045);
}

TEST(BinaryIOTest, WriteReadString) {
    BinaryWriter w;
    w.write_string("hello world");
    BinaryReader r{w.buffer()};
    std::string s;
    ASSERT_TRUE(r.read_string(s));
    EXPECT_EQ(s, "hello world");
    EXPECT_TRUE(r.eof());
}

TEST(BinaryIOTest, WriteReadEmptyString) {
    BinaryWriter w;
    w.write_string("");
    BinaryReader r{w.buffer()};
    std::string s = "untouched";
    ASSERT_TRUE(r.read_string(s));
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(r.eof());
}

TEST(BinaryIOTest, WriteReadRawBytes) {
    const uint8_t data[4] = {0x10, 0x20, 0x30, 0x40};
    BinaryWriter w;
    w.write_raw(data, sizeof(data));
    BinaryReader r{w.buffer()};
    uint8_t out[4] = {};
    ASSERT_TRUE(r.read_raw(out, sizeof(out)));
    EXPECT_EQ(out[0], 0x10);
    EXPECT_EQ(out[1], 0x20);
    EXPECT_EQ(out[2], 0x30);
    EXPECT_EQ(out[3], 0x40);
    EXPECT_TRUE(r.eof());
}

// ===========================================================================
// BinaryReader bounds checking
// ===========================================================================

TEST(BinaryIOBoundsTest, TruncatedU32ReturnsFalse) {
    BinaryWriter w;
    w.write_u16(0x1234);  // only 2 bytes available
    BinaryReader r{w.buffer()};
    uint32_t v = 0;
    EXPECT_FALSE(r.read_u32(v));  // needs 4 bytes, only 2 available
    EXPECT_EQ(v, 0u);  // output left unchanged on failure
}

TEST(BinaryIOBoundsTest, TruncatedStringReturnsFalse) {
    BinaryWriter w;
    w.write_u32(100);  // declares 100 bytes follow, but buffer ends here
    BinaryReader r{w.buffer()};
    std::string s = "untouched";
    EXPECT_FALSE(r.read_string(s));
    EXPECT_EQ(s, "untouched");  // output left unchanged on failure
}

TEST(BinaryIOBoundsTest, EmptyReaderReturnsFalseOnAnyRead) {
    std::vector<uint8_t> empty;
    BinaryReader r{empty};
    uint32_t v = 42;
    EXPECT_FALSE(r.read_u32(v));
    EXPECT_EQ(v, 42);  // unchanged
    EXPECT_TRUE(r.eof());
}

TEST(BinaryIOBoundsTest, TruncatedRawReturnsFalse) {
    BinaryWriter w;
    w.write_u8(0x01);  // only 1 byte
    BinaryReader r{w.buffer()};
    uint8_t out[4] = {};
    EXPECT_FALSE(r.read_raw(out, 4));
    // On failure the cursor is NOT advanced (read_raw bails before
    // moving the cursor), so the 1 byte we did write is still unread
    // and eof() is false. This documents the contract: failed reads
    // leave the reader positioned where it was before the call.
    EXPECT_FALSE(r.eof());
    EXPECT_EQ(r.remaining(), 1u);
}

// ===========================================================================
// Multi-field write order
// ===========================================================================

TEST(BinaryIOOrderTest, FieldsPreserveWriteOrder) {
    // Write u8, u32, string, f32 in that order; reader must consume
    // in the same order.
    BinaryWriter w;
    w.write_u8(0x01);
    w.write_u32(0x02020202);
    w.write_string("middle");
    w.write_f32(1.5f);

    BinaryReader r{w.buffer()};
    uint8_t a = 0;
    uint32_t b = 0;
    std::string c;
    float d = 0.0f;
    ASSERT_TRUE(r.read_u8(a));
    ASSERT_TRUE(r.read_u32(b));
    ASSERT_TRUE(r.read_string(c));
    ASSERT_TRUE(r.read_f32(d));
    EXPECT_EQ(a, 0x01);
    EXPECT_EQ(b, 0x02020202u);
    EXPECT_EQ(c, "middle");
    EXPECT_FLOAT_EQ(d, 1.5f);
    EXPECT_TRUE(r.eof());
}

// ===========================================================================
// Serializer<T> specializations
// ===========================================================================

TEST(SerializerTest, TransformRoundTrip) {
    Transform original;
    original.position[0] = 1.0f;
    original.position[1] = 2.0f;
    original.position[2] = 3.0f;
    original.rotation[0] = 10.0f;
    original.rotation[1] = 20.0f;
    original.rotation[2] = 30.0f;
    original.scale[0] = 0.5f;
    original.scale[1] = 1.5f;
    original.scale[2] = 2.5f;

    BinaryWriter w;
    Serializer<Transform>::write(w, original);
    ASSERT_EQ(w.size(), 36u);  // 9 floats * 4 bytes

    BinaryReader r{w.buffer()};
    Transform restored;
    ASSERT_TRUE(Serializer<Transform>::read(r, restored));

    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(restored.position[i], original.position[i]);
        EXPECT_FLOAT_EQ(restored.rotation[i], original.rotation[i]);
        EXPECT_FLOAT_EQ(restored.scale[i],     original.scale[i]);
    }
    EXPECT_TRUE(r.eof());
}

TEST(SerializerTest, TransformDefaultIsRestoredFromZeroedBuffer) {
    // Reading from an all-zero buffer should yield default-constructed
    // Transform (all zeros + scale 1,1,1). This verifies the reader
    // overwrites existing fields instead of merging.
    BinaryWriter w;
    // Write a Transform with all zeros (position 0, rotation 0, scale 0).
    // This is NOT the default (default scale is 1,1,1), so reading back
    // must set scale to 0,0,0 — confirming the reader overwrites rather
    // than merges.
    Transform zeroed;
    zeroed.scale[0] = zeroed.scale[1] = zeroed.scale[2] = 0.0f;
    Serializer<Transform>::write(w, zeroed);

    BinaryReader r{w.buffer()};
    Transform restored;  // default: scale = {1,1,1}
    ASSERT_TRUE(Serializer<Transform>::read(r, restored));
    EXPECT_FLOAT_EQ(restored.scale[0], 0.0f);  // overwritten, not merged
    EXPECT_FLOAT_EQ(restored.scale[1], 0.0f);
    EXPECT_FLOAT_EQ(restored.scale[2], 0.0f);
}

TEST(SerializerTest, MeshRefRoundTrip) {
    MeshRef original{MeshHandle{0xCAFEBABEu}};

    BinaryWriter w;
    Serializer<MeshRef>::write(w, original);
    ASSERT_EQ(w.size(), 4u);  // single u32

    BinaryReader r{w.buffer()};
    MeshRef restored;
    ASSERT_TRUE(Serializer<MeshRef>::read(r, restored));
    EXPECT_EQ(restored.handle.id, 0xCAFEBABEu);
    EXPECT_TRUE(r.eof());
}

TEST(SerializerTest, CameraRoundTrip) {
    Camera original;
    original.fov = 75.0f;
    original.near_plane = 0.5f;
    original.far_plane = 500.0f;
    original.aspect = 21.0f / 9.0f;

    BinaryWriter w;
    Serializer<Camera>::write(w, original);
    ASSERT_EQ(w.size(), 16u);  // 4 floats

    BinaryReader r{w.buffer()};
    Camera restored;
    ASSERT_TRUE(Serializer<Camera>::read(r, restored));
    EXPECT_FLOAT_EQ(restored.fov, 75.0f);
    EXPECT_FLOAT_EQ(restored.near_plane, 0.5f);
    EXPECT_FLOAT_EQ(restored.far_plane, 500.0f);
    EXPECT_FLOAT_EQ(restored.aspect, 21.0f / 9.0f);
    EXPECT_TRUE(r.eof());
}

TEST(SerializerTest, EntityGuidRoundTrip) {
    EntityGuid original{0x0123456789ABCDEFull};

    BinaryWriter w;
    Serializer<EntityGuid>::write(w, original);
    ASSERT_EQ(w.size(), 8u);  // single u64

    BinaryReader r{w.buffer()};
    EntityGuid restored;
    ASSERT_TRUE(Serializer<EntityGuid>::read(r, restored));
    EXPECT_EQ(restored.value, 0x0123456789ABCDEFull);
    EXPECT_EQ(restored, original);
    EXPECT_TRUE(r.eof());
}

TEST(SerializerTest, EntityGuidInvalidRoundTrip) {
    EntityGuid original{kInvalidEntityGuid};  // value = 0

    BinaryWriter w;
    Serializer<EntityGuid>::write(w, original);
    BinaryReader r{w.buffer()};
    EntityGuid restored{0x12345678};  // non-zero starting state
    ASSERT_TRUE(Serializer<EntityGuid>::read(r, restored));
    EXPECT_FALSE(restored.valid());
    EXPECT_EQ(restored, kInvalidEntityGuid);
}

// ===========================================================================
// Truncated component reads
// ===========================================================================

TEST(SerializerTruncationTest, TransformTruncatedReturnsFalse) {
    // Write only the first 12 bytes (position) of a Transform; reading
    // back must fail because rotation + scale are missing.
    BinaryWriter w;
    float pos[3] = {1.0f, 2.0f, 3.0f};
    w.write_raw(pos, sizeof(pos));

    BinaryReader r{w.buffer()};
    Transform t;
    EXPECT_FALSE(Serializer<Transform>::read(r, t));
    // Position was read successfully before the failure.
    EXPECT_FLOAT_EQ(t.position[0], 1.0f);
    EXPECT_FLOAT_EQ(t.position[1], 2.0f);
    EXPECT_FLOAT_EQ(t.position[2], 3.0f);
}

TEST(SerializerTruncationTest, CameraTruncatedReturnsFalse) {
    // Write only 3 of the 4 floats; reading back must fail.
    BinaryWriter w;
    w.write_f32(75.0f);
    w.write_f32(0.5f);
    w.write_f32(500.0f);
    // Missing: aspect.

    BinaryReader r{w.buffer()};
    Camera c;
    EXPECT_FALSE(Serializer<Camera>::read(r, c));
    EXPECT_FLOAT_EQ(c.fov, 75.0f);  // first 3 read OK
}

// ===========================================================================
// Combined: serialize multiple components + Guid in sequence
// ===========================================================================

TEST(SerializerCombinedTest, MultipleComponentsInSequence) {
    // Simulate writing an entity: Guid + Transform + MeshRef + Camera.
    EntityGuid guid{0xABCDEF0123456789ull};
    Transform tf;
    tf.position[0] = 5.0f;
    tf.position[1] = 10.0f;
    tf.position[2] = -3.0f;
    MeshRef mesh{MeshHandle{7u}};
    Camera cam;
    cam.fov = 90.0f;

    BinaryWriter w;
    Serializer<EntityGuid>::write(w, guid);
    Serializer<Transform>::write(w, tf);
    Serializer<MeshRef>::write(w, mesh);
    Serializer<Camera>::write(w, cam);

    BinaryReader r{w.buffer()};

    EntityGuid g2;
    Transform tf2;
    MeshRef m2;
    Camera c2;
    ASSERT_TRUE(Serializer<EntityGuid>::read(r, g2));
    ASSERT_TRUE(Serializer<Transform>::read(r, tf2));
    ASSERT_TRUE(Serializer<MeshRef>::read(r, m2));
    ASSERT_TRUE(Serializer<Camera>::read(r, c2));

    EXPECT_EQ(g2, guid);
    EXPECT_FLOAT_EQ(tf2.position[0], 5.0f);
    EXPECT_FLOAT_EQ(tf2.position[1], 10.0f);
    EXPECT_FLOAT_EQ(tf2.position[2], -3.0f);
    EXPECT_EQ(m2.handle.id, 7u);
    EXPECT_FLOAT_EQ(c2.fov, 90.0f);
    EXPECT_TRUE(r.eof());
}

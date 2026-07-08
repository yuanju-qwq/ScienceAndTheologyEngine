// Uuid implementation.

#include "core/uuid.h"

#include "core/binary_reader.h"
#include "core/binary_writer.h"

#include <chrono>
#include <random>

namespace snt::core {

namespace {

// Mix two 32-bit values into one via an xorshift-ish twiddle. Cheap and
// good enough for Uuid seed uniqueness within a single process run.
// Mirrors the helper in entity_guid.cpp so both generators share the
// same mixing quality.
uint32_t mix_seed(uint32_t a, uint32_t b) {
    uint32_t x = a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
    x ^= x >> 17;
    x *= 0xed5ad4bbu;
    x ^= x >> 11;
    x *= 0xac4c1b51u;
    x ^= x >> 15;
    x *= 0x31848babu;
    x ^= x >> 14;
    return x;
}

}  // namespace

UuidGenerator::UuidGenerator() {
    // 128-bit seed = 4x 32-bit mixes of steady_clock + random_device.
    // Two generators constructed in the same process will (with extremely
    // high probability) get different seeds, so their Uuids never collide.
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device rd;
    const uint32_t r1 = rd();
    const uint32_t r2 = rd();
    const uint32_t r3 = rd();
    const uint32_t r4 = rd();

    const uint32_t now_lo = static_cast<uint32_t>(now);
    const uint32_t now_hi = static_cast<uint32_t>(now >> 32);

    seed_high_ = (static_cast<uint64_t>(mix_seed(now_hi, r1)) << 32) |
                 static_cast<uint64_t>(mix_seed(now_lo, r2));
    seed_low_  = (static_cast<uint64_t>(mix_seed(r3, now_lo)) << 32) |
                 static_cast<uint64_t>(mix_seed(r4, now_hi));

    // Guard against the astronomically unlikely case of an all-zero seed
    // (which would make the first Uuid collide with kInvalidUuid on
    // counter==0; we already skip counter 0 in next(), but keeping the
    // seed nonzero keeps the Uuid space uniform).
    if (seed_high_ == 0 && seed_low_ == 0) {
        seed_high_ = 1;
    }
}

Uuid UuidGenerator::next() {
    ++counter_;  // skip 0; first issued Uuid uses counter=1
    return pack();
}

Uuid UuidGenerator::peek_next() const {
    // next() will pre-increment counter_, so the next issued Uuid packs
    // (counter_ + 1).
    UuidGenerator copy = *this;
    return copy.next();
}

void UuidGenerator::reset_counter(uint64_t first) {
    counter_ = first;
}

Uuid UuidGenerator::pack() const {
    // Layout:
    //   high = seed_high_                         (pure seed, never changes)
    //   low  = seed_low_ ^ counter_               (seed folded with counter)
    //
    // XOR-folding the counter into seed_low_ ensures two generators
    // that happen to issue the same counter value still produce
    // disjoint Uuids (because their seed_low_ differs). This is
    // stronger than EntityGuid's layout (which keeps counter pure in
    // the low bits) because Uuid's 128-bit space can afford the fold
    // without practical collision risk.
    return Uuid{
        /* low  */ seed_low_ ^ counter_,
        /* high */ seed_high_,
    };
}

}  // namespace snt::core

// ---------------------------------------------------------------------------
// Serializer<Uuid> implementation.
// ---------------------------------------------------------------------------
// Defined out-of-line to keep uuid.h dependency-light (it only
// forward-declares BinaryWriter / BinaryReader). The .cpp pulls in the
// full definitions.
namespace snt::core {

void Serializer<snt::core::Uuid>::write(
    BinaryWriter& w, const snt::core::Uuid& u) {
    w.write_u64(u.low);
    w.write_u64(u.high);
}

bool Serializer<snt::core::Uuid>::read(
    BinaryReader& r, snt::core::Uuid& u) {
    if (!r.read_u64(u.low))  return false;
    if (!r.read_u64(u.high)) return false;
    return true;
}

}  // namespace snt::core

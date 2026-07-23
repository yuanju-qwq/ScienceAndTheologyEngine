// C++ facade for the Zig-owned UUID generator state machine.

#define SNT_LOG_CHANNEL "uuid"
#include "core/uuid.h"

#include "core/binary_reader.h"
#include "core/binary_writer.h"
#include "core/log.h"

#include <chrono>
#include <exception>
#include <random>

namespace snt::core {

namespace {

SntUuidGeneratorEntropy gather_host_entropy() {
    SntUuidGeneratorEntropy entropy = SNT_UUID_GENERATOR_ENTROPY_INIT;
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device random_device;

    entropy.steady_clock_ticks = static_cast<uint64_t>(now);
    for (uint32_t& word : entropy.random_words) {
        word = static_cast<uint32_t>(random_device());
    }
    return entropy;
}

[[noreturn]] void fail_uuid_abi_call(const char* operation, SntAbiStatus status) {
    SNT_LOG_FATAL("%s failed: %s", operation, snt_abi_status_message(status));
    std::terminate();
}

}  // namespace

UuidGenerator::UuidGenerator() {
    const SntUuidGeneratorEntropy entropy = gather_host_entropy();
    const SntAbiStatus status = snt_uuid_generator_initialize(&state_, &entropy);
    if (status != SNT_ABI_STATUS_OK) {
        fail_uuid_abi_call("snt_uuid_generator_initialize", status);
    }
}

Uuid UuidGenerator::next() {
    SntUuid value = SNT_UUID_INIT;
    const SntAbiStatus status = snt_uuid_generator_next(&state_, &value);
    if (status != SNT_ABI_STATUS_OK) {
        fail_uuid_abi_call("snt_uuid_generator_next", status);
    }
    return {value.low, value.high};
}

Uuid UuidGenerator::peek_next() const {
    SntUuid value = SNT_UUID_INIT;
    const SntAbiStatus status = snt_uuid_generator_peek_next(&state_, &value);
    if (status != SNT_ABI_STATUS_OK) {
        fail_uuid_abi_call("snt_uuid_generator_peek_next", status);
    }
    return {value.low, value.high};
}

void UuidGenerator::reset_counter(uint64_t first) {
    const SntAbiStatus status = snt_uuid_generator_reset_counter(&state_, first);
    if (status != SNT_ABI_STATUS_OK) {
        fail_uuid_abi_call("snt_uuid_generator_reset_counter", status);
    }
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

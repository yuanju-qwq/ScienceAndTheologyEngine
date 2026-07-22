#include "abi/render_snapshot_abi.h"
#include "abi/runtime_abi.h"

#include <gtest/gtest.h>

#include <cstddef>

extern "C" SntAbiStatus snt_abi_c_smoke_query(SntRuntimeAbiDescriptor* descriptor);
extern "C" uint32_t snt_abi_c_smoke_snapshot_layout_is_valid(void);

namespace {

TEST(RuntimeAbi, DescriptorCanBeQueriedByCConsumer) {
    SntRuntimeAbiDescriptor descriptor{};

    ASSERT_EQ(snt_abi_c_smoke_query(&descriptor), SNT_ABI_STATUS_OK);
    EXPECT_EQ(descriptor.abi_major, SNT_RUNTIME_ABI_MAJOR);
    EXPECT_EQ(descriptor.abi_minor, SNT_RUNTIME_ABI_MINOR);
    EXPECT_EQ(descriptor.runtime_descriptor_size, sizeof(SntRuntimeAbiDescriptor));
    EXPECT_EQ(descriptor.capabilities, SNT_RUNTIME_ABI_CAPABILITY_DESCRIPTOR_QUERY);
    EXPECT_STREQ(snt_abi_status_message(SNT_ABI_STATUS_OK), "ok");
}

TEST(RuntimeAbi, RejectsDescriptorsWithoutStablePrefix) {
    SntRuntimeAbiDescriptor descriptor{};
    descriptor.struct_size = static_cast<uint32_t>(
        offsetof(SntRuntimeAbiDescriptor, runtime_descriptor_size));

    EXPECT_EQ(snt_runtime_abi_query_descriptor(&descriptor), SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(snt_runtime_abi_query_descriptor(nullptr), SNT_ABI_STATUS_INVALID_ARGUMENT);
    EXPECT_STREQ(snt_abi_status_message(SNT_ABI_STATUS_INVALID_ARGUMENT), "invalid argument");
}

TEST(RenderSnapshotAbi, IsCCompatibleValueOnlyContract) {
    EXPECT_EQ(snt_abi_c_smoke_snapshot_layout_is_valid(), 1u);

    SntRenderSnapshotView snapshot = SNT_RENDER_SNAPSHOT_VIEW_INIT;
    EXPECT_EQ(snapshot.struct_size, sizeof(SntRenderSnapshotView));
    EXPECT_EQ(snapshot.schema_version, SNT_RENDER_SNAPSHOT_SCHEMA_VERSION);
    EXPECT_EQ(snapshot.payload.data, nullptr);
    EXPECT_EQ(snapshot.payload.size_bytes, 0u);
}

}  // namespace

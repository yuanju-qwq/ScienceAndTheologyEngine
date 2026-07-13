// Tests for the root-owned filesystem implementation of IAssetSource.

#include "assets/filesystem_asset_source.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

class FilesystemAssetSourceTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        root_ = fs::temp_directory_path() /
            (std::string("snt_filesystem_asset_source_") +
             info->test_suite_name() + "_" + info->name());

        std::error_code error;
        fs::remove_all(root_, error);
        ASSERT_FALSE(error) << error.message();
        fs::create_directories(root_, error);
        ASSERT_FALSE(error) << error.message();
    }

    void TearDown() override {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    void write_file(const fs::path& relative_path,
                    const std::vector<std::uint8_t>& bytes) {
        const fs::path path = root_ / relative_path;
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
        ASSERT_FALSE(error) << error.message();

        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output.is_open()) << path;
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(output.good()) << path;
    }

    fs::path root_;
};

TEST_F(FilesystemAssetSourceTest, CreateValidatesAnExistingDirectoryRoot) {
    auto missing = snt::assets::FilesystemAssetSource::create(root_ / "missing");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code(), snt::core::ErrorCode::kFileNotFound);

    const fs::path regular_file = root_ / "not_a_directory.bin";
    write_file("not_a_directory.bin", {0x01});
    auto not_a_directory = snt::assets::FilesystemAssetSource::create(regular_file);
    ASSERT_FALSE(not_a_directory.has_value());
    EXPECT_EQ(not_a_directory.error().code(), snt::core::ErrorCode::kInvalidArgument);
}

TEST_F(FilesystemAssetSourceTest, ReadReturnsCanonicalOwnedBytes) {
    const fs::path asset_path = root_ / "assets" / "cube.bin";
    write_file("assets/cube.bin", {0x01, 0x7f, 0xff});

    auto source_result = snt::assets::FilesystemAssetSource::create(root_);
    ASSERT_TRUE(source_result.has_value()) << source_result.error().format();
    auto source = std::move(*source_result);

    auto result = source.read({"assets/./cube.bin"});
    ASSERT_TRUE(result.has_value()) << result.error().format();
    EXPECT_EQ(result->canonical_path, fs::canonical(asset_path).generic_string());
    EXPECT_EQ(result->bytes, (std::vector<std::uint8_t>{0x01, 0x7f, 0xff}));

    result->bytes[0] = 0x00;
    auto reread = source.read({"assets/cube.bin"});
    ASSERT_TRUE(reread.has_value()) << reread.error().format();
    EXPECT_EQ(reread->bytes[0], 0x01);
}

TEST_F(FilesystemAssetSourceTest, ReadRejectsRootedTraversalAndMissingPaths) {
    auto source_result = snt::assets::FilesystemAssetSource::create(root_);
    ASSERT_TRUE(source_result.has_value()) << source_result.error().format();
    auto source = std::move(*source_result);

    auto absolute = source.read({(root_ / "outside.bin").string()});
    ASSERT_FALSE(absolute.has_value()) << absolute.error().format();
    EXPECT_EQ(absolute.error().code(), snt::core::ErrorCode::kInvalidArgument);

#if defined(_WIN32)
    auto drive_relative = source.read({"C:outside.bin"});
    ASSERT_FALSE(drive_relative.has_value()) << drive_relative.error().format();
    EXPECT_EQ(drive_relative.error().code(), snt::core::ErrorCode::kInvalidArgument);

    auto drive_rooted = source.read({R"(\\outside.bin)"});
    ASSERT_FALSE(drive_rooted.has_value()) << drive_rooted.error().format();
    EXPECT_EQ(drive_rooted.error().code(), snt::core::ErrorCode::kInvalidArgument);
#endif

    auto traversal = source.read({"../outside.bin"});
    ASSERT_FALSE(traversal.has_value()) << traversal.error().format();
    EXPECT_EQ(traversal.error().code(), snt::core::ErrorCode::kInvalidArgument);

    auto missing = source.read({"assets/missing.bin"});
    ASSERT_FALSE(missing.has_value()) << missing.error().format();
    EXPECT_EQ(missing.error().code(), snt::core::ErrorCode::kFileNotFound);
}

}  // namespace

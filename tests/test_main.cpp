// GoogleTest main entry for the SNT engine test suite.
//
// Provides the standard `RUN_ALL_TESTS()` driver. Individual test TUs
// (test_*.cpp) define TEST(...) blocks; GoogleTest collects them at link
// time. Keeping a dedicated test_main.cpp lets us add global setup/teardown
// later (e.g. initializing path_utils before any test runs) without
// touching every test file.

#include <gtest/gtest.h>

// Global test environment: runs init once before any test, cleanup once
// after all tests. Used here to initialize path_utils so tests that touch
// the filesystem resolve paths the same way the engine does.
class SntTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // path_utils::init is idempotent; failure is non-fatal for tests
        // (tests that depend on it will check engine_root() themselves).
    }
    void TearDown() override {}
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new SntTestEnvironment());
    return RUN_ALL_TESTS();
}

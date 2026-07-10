// P7.1 tests -- gameplay Script API, FileWatcher, and transactional reload.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "script/file_watcher.h"
#include "script/script_manager.h"

namespace fs = std::filesystem;

namespace {

using snt::script::FileChange;
using snt::script::FileChangeKind;
using snt::script::ScriptManager;

class TempScriptDirectory {
public:
    explicit TempScriptDirectory(const std::string& name)
        : root_(fs::temp_directory_path() / ("snt_p7_" + name)) {
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
    }

    ~TempScriptDirectory() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    void write(const std::string& name, const std::string& source) const {
        const fs::path path = root_ / name;
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << source;
    }

    void remove(const std::string& name) const {
        std::error_code ec;
        fs::remove(root_ / name, ec);
    }

    const fs::path& root() const { return root_; }

private:
    fs::path root_;
};

std::string gameplay_source(const std::string& input_item,
                            const std::string& callback_id,
                            bool define_callback) {
    std::string source =
        "void snt_register() {\n"
        "    snt_register_recipe(\"p7.test.recipe\", \"furnace\", \"" + input_item +
        "\", \"test_ingot\", 1, 20, 0, \"p7-test\");\n"
        "    snt_on(\"p7.test.tick\", \"" + callback_id + "\");\n"
        "}\n";
    if (define_callback) {
        source += "void " + callback_id + "() {}\n";
    }
    return source;
}

}  // namespace

TEST(P7FileWatcherTest, ReportsFilteredChangesInStablePathOrder) {
    TempScriptDirectory directory("watcher");
    directory.write("first.as", "void snt_register() {}");

    auto watcher = snt::script::create_polling_file_watcher();
    ASSERT_TRUE(watcher->start(directory.root(), {".as"}));
    EXPECT_TRUE(watcher->drain_changes().empty());

    directory.write("first.as", "void snt_register() { snt_log(\"changed\"); }");
    directory.write("second.as", "void snt_register() {}");
    directory.write("ignored.txt", "not a script");

    const std::vector<FileChange> changes = watcher->drain_changes();
    ASSERT_EQ(changes.size(), 2U);
    EXPECT_EQ(changes[0].path.filename(), "first.as");
    EXPECT_EQ(changes[0].kind, FileChangeKind::Modified);
    EXPECT_EQ(changes[1].path.filename(), "second.as");
    EXPECT_EQ(changes[1].kind, FileChangeKind::Created);

    directory.remove("first.as");
    const std::vector<FileChange> removed = watcher->drain_changes();
    ASSERT_EQ(removed.size(), 1U);
    EXPECT_EQ(removed[0].path.filename(), "first.as");
    EXPECT_EQ(removed[0].kind, FileChangeKind::Removed);
}

TEST(P7ScriptApiTest, ScriptRegistersCopiedGameplayDefinition) {
    auto& scripts = ScriptManager::instance();
    ASSERT_TRUE(scripts.init());
    ASSERT_TRUE(scripts.load_source(
        "p7_api",
        "void snt_register() {"
        "  snt_register_machine(\"p7.test.machine\", \"Test Machine\", 2, 500);"
        "  snt_register_quest(\"p7.test.quest\", \"Test Quest\", \"Description\");"
        "}"));

    const auto* machine = scripts.registries().find_machine("p7.test.machine");
    ASSERT_NE(machine, nullptr);
    EXPECT_EQ(machine->display_name, "Test Machine");
    EXPECT_EQ(machine->tier, 2);
    EXPECT_EQ(machine->power_capacity, 500);
    EXPECT_NE(scripts.registries().find_quest("p7.test.quest"), nullptr);
    EXPECT_FALSE(scripts.execute_command("/snt unknown"));
    scripts.shutdown();
}

TEST(P7ScriptReloadTest, WatcherCommitsSuccessAndRollsBackFailuresWithoutStaleCallbacks) {
    TempScriptDirectory directory("transaction");
    directory.write("gameplay.as", gameplay_source("ore_a", "on_tick", true));

    auto& scripts = ScriptManager::instance();
    ASSERT_TRUE(scripts.init());
    ASSERT_TRUE(scripts.watch_directory(directory.root()));

    ASSERT_NE(scripts.registries().find_recipe("p7.test.recipe"), nullptr);
    EXPECT_EQ(scripts.registries().find_recipe("p7.test.recipe")->input_item_id, "ore_a");

    directory.write("gameplay.as", gameplay_source("ore_b", "on_tick", true));
    scripts.update(0.016f);
    ASSERT_NE(scripts.registries().find_recipe("p7.test.recipe"), nullptr);
    EXPECT_EQ(scripts.registries().find_recipe("p7.test.recipe")->input_item_id, "ore_b");

    directory.write("gameplay.as", "void snt_register() {");
    scripts.update(0.016f);
    ASSERT_NE(scripts.registries().find_recipe("p7.test.recipe"), nullptr);
    EXPECT_EQ(scripts.registries().find_recipe("p7.test.recipe")->input_item_id, "ore_b");

    directory.write("gameplay.as", gameplay_source("ore_bad", "missing_callback", false));
    scripts.update(0.016f);
    ASSERT_NE(scripts.registries().find_recipe("p7.test.recipe"), nullptr);
    EXPECT_EQ(scripts.registries().find_recipe("p7.test.recipe")->input_item_id, "ore_b");
    const auto listeners = scripts.registries().event_listeners("p7.test.tick");
    ASSERT_EQ(listeners.size(), 1U);
    EXPECT_EQ(listeners[0].callback_id, "on_tick");

    scripts.shutdown();
}

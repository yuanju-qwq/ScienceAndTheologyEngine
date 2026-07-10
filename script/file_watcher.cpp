// FileWatcher -- deterministic main-thread polling implementation.

#define SNT_LOG_CHANNEL "script"
#include "script/file_watcher.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>
#include <utility>

#include "core/error.h"
#include "core/log.h"

namespace snt::script {

namespace fs = std::filesystem;

namespace {

struct FileStamp {
    fs::path path;
    fs::file_time_type write_time{};
    uintmax_t size = 0;
};

using Snapshot = std::map<std::string, FileStamp, std::less<>>;

std::string normalize_extension(std::string extension) {
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

class PollingFileWatcher final : public FileWatcher {
public:
    snt::core::Expected<void> start(const fs::path& root,
                                    std::vector<std::string> extensions) override {
        if (running_) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "FileWatcher is already running"};
        }
        if (extensions.empty()) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "FileWatcher requires at least one extension"};
        }

        std::error_code ec;
        root_ = fs::absolute(root, ec);
        if (ec || !fs::is_directory(root_, ec)) {
            return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                    "Script watch root is not a directory: " + root.string()};
        }

        extensions_.clear();
        extensions_.reserve(extensions.size());
        for (std::string& extension : extensions) {
            extensions_.push_back(normalize_extension(std::move(extension)));
        }
        std::sort(extensions_.begin(), extensions_.end());
        extensions_.erase(std::unique(extensions_.begin(), extensions_.end()), extensions_.end());

        snapshot_ = scan();
        running_ = true;
        SNT_LOG_INFO("Watching %s for %zu script extension(s)",
                     root_.string().c_str(), extensions_.size());
        return {};
    }

    void stop() override {
        running_ = false;
        root_.clear();
        extensions_.clear();
        snapshot_.clear();
    }

    bool running() const override { return running_; }

    std::vector<FileChange> drain_changes() override {
        if (!running_) {
            return {};
        }

        Snapshot current = scan();
        std::vector<FileChange> changes;

        for (const auto& [key, previous] : snapshot_) {
            const auto now = current.find(key);
            if (now == current.end()) {
                changes.push_back(FileChange{previous.path, FileChangeKind::Removed});
                continue;
            }
            if (previous.write_time != now->second.write_time || previous.size != now->second.size) {
                changes.push_back(FileChange{now->second.path, FileChangeKind::Modified});
            }
        }
        for (const auto& [key, current_file] : current) {
            if (!snapshot_.contains(key)) {
                changes.push_back(FileChange{current_file.path, FileChangeKind::Created});
            }
        }

        snapshot_ = std::move(current);
        std::sort(changes.begin(), changes.end(), [](const FileChange& lhs, const FileChange& rhs) {
            return lhs.path.generic_string() < rhs.path.generic_string();
        });
        return changes;
    }

private:
    bool matches_extension(const fs::path& path) const {
        return std::binary_search(extensions_.begin(), extensions_.end(),
                                  normalize_extension(path.extension().string()));
    }

    Snapshot scan() const {
        Snapshot files;
        std::error_code ec;
        fs::recursive_directory_iterator it(
            root_, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        while (!ec && it != end) {
            const fs::directory_entry entry = *it;
            it.increment(ec);

            std::error_code file_ec;
            if (file_ec || !entry.is_regular_file(file_ec) || !matches_extension(entry.path())) {
                continue;
            }

            const fs::path path = fs::absolute(entry.path(), file_ec);
            if (file_ec) {
                continue;
            }
            const fs::file_time_type write_time = entry.last_write_time(file_ec);
            if (file_ec) {
                continue;
            }
            const uintmax_t size = entry.file_size(file_ec);
            if (file_ec) {
                continue;
            }
            files.emplace(path.generic_string(), FileStamp{path, write_time, size});
        }
        return files;
    }

    bool running_ = false;
    fs::path root_;
    std::vector<std::string> extensions_;
    Snapshot snapshot_;
};

}  // namespace

std::unique_ptr<FileWatcher> create_polling_file_watcher() {
    return std::make_unique<PollingFileWatcher>();
}

}  // namespace snt::script

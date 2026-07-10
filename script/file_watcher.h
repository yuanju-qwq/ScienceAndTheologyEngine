// FileWatcher -- P7 platform-neutral script change source.
//
// The initial P7 implementation will use this contract behind ScriptManager.
// Platform-specific notifications (ReadDirectoryChangesW/inotify/FSEvents) are
// intentionally hidden here; polling remains a valid implementation fallback.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/expected.h"

namespace snt::script {

enum class FileChangeKind : uint8_t {
    Created,
    Modified,
    Removed,
    Renamed,
};

struct FileChange {
    std::filesystem::path path;
    FileChangeKind kind = FileChangeKind::Modified;
};

class FileWatcher {
public:
    virtual ~FileWatcher() = default;

    // Starts watching one script root. The implementation must enqueue only
    // matching extensions and never call ScriptManager from its worker thread.
    virtual snt::core::Expected<void> start(const std::filesystem::path& root,
                                            std::vector<std::string> extensions) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;

    // Called by ScriptManager on the main thread; returns and clears queued
    // changes in a stable path order.
    virtual std::vector<FileChange> drain_changes() = 0;
};

// P7.1 default implementation. It snapshots the watched tree when started,
// then compares timestamps and sizes when the main thread drains changes.
// A polling implementation is deliberately used until native notifications
// are required; its queue contract stays identical across future backends.
std::unique_ptr<FileWatcher> create_polling_file_watcher();

}  // namespace snt::script

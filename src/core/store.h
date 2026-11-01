// Reads and writes the app's data files in the user's data directory.
#pragma once

#include "model.h"
#include "workspace.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fetcher {

// Writes via a temporary file and a rename, so a crash mid-write can never
// leave a half-written file behind. Files are readable by the owner only,
// because saved requests may contain credentials. Throws std::runtime_error.
void writeFileAtomic(const std::filesystem::path& path, const std::string& data);

// Throws std::runtime_error if the file cannot be read.
std::string readFile(const std::filesystem::path& path);

class Store {
public:
    static constexpr std::size_t kMaxHistory = 200;

    explicit Store(std::filesystem::path directory);

    const std::filesystem::path& directory() const { return directory_; }

    // A missing file gives an empty workspace. An unreadable or damaged file
    // is renamed aside rather than overwritten by the next save, and the
    // returned text explains what happened to the user. Empty means no problem.
    std::string load(Workspace& workspace);
    std::string load(std::vector<HistoryEntry>& history);

    // Both throw std::runtime_error if the write fails.
    void save(const Workspace& workspace);
    void save(const std::vector<HistoryEntry>& history);

private:
    std::filesystem::path setAside(const std::filesystem::path& path);

    std::filesystem::path directory_;
};

}  // namespace fetcher

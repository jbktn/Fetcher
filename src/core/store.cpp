#include "store.h"

#include "serialize.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace fetcher {

void writeFileAtomic(const fs::path& path, const std::string& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) throw std::runtime_error("Cannot create " + path.parent_path().u8string() + ": " + ec.message());

    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot write " + temporary.u8string());
        // Restrict access before any contents are written. No effect on Windows,
        // where the user's profile directory is already private.
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write, ec);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) throw std::runtime_error("Cannot write " + temporary.u8string());
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(temporary, ec);
        throw std::runtime_error("Cannot replace " + path.u8string());
    }
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read " + path.u8string());
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

Store::Store(fs::path directory) : directory_(std::move(directory)) {}

fs::path Store::setAside(const fs::path& path) {
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path aside = path;
    aside += ".damaged-" + std::to_string(stamp);
    std::error_code ec;
    fs::rename(path, aside, ec);
    return ec ? fs::path() : aside;
}

std::string Store::load(Workspace& workspace) {
    const fs::path path = directory_ / "collections.json";
    std::error_code ec;
    if (!fs::exists(path, ec)) return {};
    try {
        workspace.replaceRoot(folderFromJson(readFile(path)));
        return {};
    } catch (const std::exception& e) {
        const fs::path aside = setAside(path);
        return "Your saved requests could not be loaded (" + std::string(e.what()) + ").\n\n" +
               (aside.empty() ? "The file is at " + path.u8string()
                              : "The file was kept as " + aside.u8string()) +
               "\nFetcher is starting with an empty collection.";
    }
}

std::string Store::load(std::vector<HistoryEntry>& history) {
    const fs::path path = directory_ / "history.json";
    std::error_code ec;
    if (!fs::exists(path, ec)) return {};
    try {
        history = historyFromJson(readFile(path));
        return {};
    } catch (const std::exception& e) {
        const fs::path aside = setAside(path);
        return "Your request history could not be loaded (" + std::string(e.what()) + ").\n\n" +
               (aside.empty() ? "The file is at " + path.u8string()
                              : "The file was kept as " + aside.u8string());
    }
}

void Store::save(const Workspace& workspace) {
    writeFileAtomic(directory_ / "collections.json", folderToJson(workspace.root()));
}

void Store::save(const std::vector<HistoryEntry>& history) {
    writeFileAtomic(directory_ / "history.json", historyToJson(history));
}

}  // namespace fetcher

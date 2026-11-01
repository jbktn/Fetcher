#include "workspace.h"

#include <algorithm>
#include <random>
#include <utility>

namespace fetcher {

std::string newId() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static const char digits[] = "0123456789abcdef";
    std::string id(16, '0');
    std::uint64_t value = rng();
    for (auto& c : id) {
        c = digits[value & 0xF];
        value >>= 4;
    }
    return id;
}

namespace {

Folder* findFolderIn(Folder& folder, const std::string& id) {
    if (folder.id == id) return &folder;
    for (auto& child : folder.folders)
        if (Folder* found = findFolderIn(child, id)) return found;
    return nullptr;
}

Folder* findFolderParent(Folder& folder, const std::string& id) {
    for (auto& child : folder.folders) {
        if (child.id == id) return &folder;
        if (Folder* found = findFolderParent(child, id)) return found;
    }
    return nullptr;
}

Folder* findRequestParent(Folder& folder, const std::string& id) {
    for (const auto& request : folder.requests)
        if (request.id == id) return &folder;
    for (auto& child : folder.folders)
        if (Folder* found = findRequestParent(child, id)) return found;
    return nullptr;
}

void assignFreshIds(Folder& folder) {
    folder.id = newId();
    for (auto& request : folder.requests) request.id = newId();
    for (auto& child : folder.folders) assignFreshIds(child);
}

}  // namespace

Workspace::Workspace() { root_.id = kRootId; }

void Workspace::replaceRoot(Folder root) {
    root_ = std::move(root);
    root_.id = kRootId;
}

Folder* Workspace::findFolder(const std::string& id) { return findFolderIn(root_, id); }
Folder* Workspace::parentOfFolder(const std::string& id) { return findFolderParent(root_, id); }
Folder* Workspace::parentOfRequest(const std::string& id) { return findRequestParent(root_, id); }

Request* Workspace::findRequest(const std::string& id) {
    Folder* parent = parentOfRequest(id);
    if (!parent) return nullptr;
    auto it = std::find_if(parent->requests.begin(), parent->requests.end(),
                           [&](const Request& r) { return r.id == id; });
    return it == parent->requests.end() ? nullptr : &*it;
}

std::string Workspace::addFolder(const std::string& parentId, const std::string& name) {
    Folder* parent = findFolder(parentId);
    if (!parent) return {};
    Folder folder;
    folder.id = newId();
    folder.name = name;
    parent->folders.push_back(std::move(folder));
    return parent->folders.back().id;
}

std::string Workspace::addRequest(const std::string& folderId, Request request) {
    Folder* folder = findFolder(folderId);
    if (!folder) return {};
    request.id = newId();
    folder->requests.push_back(std::move(request));
    return folder->requests.back().id;
}

std::string Workspace::duplicateRequest(const std::string& id) {
    Folder* parent = parentOfRequest(id);
    Request* source = findRequest(id);
    if (!parent || !source) return {};
    Request copy = *source;
    copy.id = newId();
    copy.name += " copy";
    auto it = std::find_if(parent->requests.begin(), parent->requests.end(),
                           [&](const Request& r) { return r.id == id; });
    it = parent->requests.insert(it + 1, std::move(copy));
    return it->id;
}

std::string Workspace::importFolder(const std::string& parentId, Folder folder) {
    Folder* parent = findFolder(parentId);
    if (!parent) return {};
    assignFreshIds(folder);
    parent->folders.push_back(std::move(folder));
    return parent->folders.back().id;
}

bool Workspace::updateRequest(const Request& request) {
    Request* existing = findRequest(request.id);
    if (!existing) return false;
    *existing = request;
    return true;
}

bool Workspace::renameFolder(const std::string& id, const std::string& name) {
    if (id == kRootId) return false;
    Folder* folder = findFolder(id);
    if (!folder) return false;
    folder->name = name;
    return true;
}

bool Workspace::removeFolder(const std::string& id) {
    Folder* parent = parentOfFolder(id);
    if (!parent) return false;
    auto& list = parent->folders;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Folder& f) { return f.id == id; }),
               list.end());
    return true;
}

bool Workspace::removeRequest(const std::string& id) {
    Folder* parent = parentOfRequest(id);
    if (!parent) return false;
    auto& list = parent->requests;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Request& r) { return r.id == id; }),
               list.end());
    return true;
}

bool Workspace::moveRequest(const std::string& id, const std::string& folderId) {
    Folder* from = parentOfRequest(id);
    if (!from || !findFolder(folderId)) return false;
    if (from->id == folderId) return true;
    Request moved = *findRequest(id);
    removeRequest(id);
    findFolder(folderId)->requests.push_back(std::move(moved));
    return true;
}

bool Workspace::moveFolder(const std::string& id, const std::string& newParentId) {
    Folder* folder = findFolder(id);
    Folder* from = parentOfFolder(id);
    if (!folder || !from || !findFolder(newParentId)) return false;
    if (findFolderIn(*folder, newParentId)) return false;  // into itself or a descendant
    if (from->id == newParentId) return true;
    Folder moved = std::move(*folder);
    // The moved-from entry keeps no usable id, so remove it by position.
    auto& siblings = from->folders;
    auto position = siblings.begin() + (folder - siblings.data());
    siblings.erase(position);
    findFolder(newParentId)->folders.push_back(std::move(moved));
    return true;
}

}  // namespace fetcher

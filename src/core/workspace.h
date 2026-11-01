// The tree of saved folders and requests.
//
// Pointers returned by the find functions are only valid until the next
// change to the tree, so callers should not hold on to them.
#pragma once

#include "model.h"

#include <string>

namespace fetcher {

class Workspace {
public:
    static constexpr const char* kRootId = "root";

    Workspace();

    Folder& root() { return root_; }
    const Folder& root() const { return root_; }
    void replaceRoot(Folder root);

    Folder* findFolder(const std::string& id);
    Request* findRequest(const std::string& id);
    Folder* parentOfFolder(const std::string& id);
    Folder* parentOfRequest(const std::string& id);

    // Each returns the id of the new item, or an empty string if the target
    // folder does not exist.
    std::string addFolder(const std::string& parentId, const std::string& name);
    std::string addRequest(const std::string& folderId, Request request);
    std::string duplicateRequest(const std::string& id);

    // Adds a copy of `folder` under `parentId` with fresh ids throughout, so
    // importing the same file twice never produces clashing items.
    std::string importFolder(const std::string& parentId, Folder folder);

    bool updateRequest(const Request& request);
    bool renameFolder(const std::string& id, const std::string& name);
    bool removeFolder(const std::string& id);
    bool removeRequest(const std::string& id);
    bool moveRequest(const std::string& id, const std::string& folderId);
    // Refuses to move a folder into itself or one of its own subfolders.
    bool moveFolder(const std::string& id, const std::string& newParentId);

private:
    Folder root_;
};

}  // namespace fetcher

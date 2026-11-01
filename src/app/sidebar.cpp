#include "sidebar.h"

#include "dialogs.h"
#include "widgets.h"

#include <wx/datetime.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <cctype>

namespace {

constexpr int kFolderImage = 0;

void collectRequestIds(const Folder& folder, std::vector<std::string>& ids) {
    for (const auto& request : folder.requests) ids.push_back(request.id);
    for (const auto& child : folder.folders) collectRequestIds(child, ids);
}

void collectMethods(const Folder& folder, std::set<std::string>& methods) {
    for (const auto& request : folder.requests) methods.insert(request.method);
    for (const auto& child : folder.folders) collectMethods(child, methods);
}

std::string upperCase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

// URLs read better without the scheme in a narrow column.
wxString shortUrl(const std::string& url) {
    const auto scheme = url.find("://");
    return toWx(scheme == std::string::npos ? url : url.substr(scheme + 3));
}

}  // namespace

struct Sidebar::ItemData : public wxTreeItemData {
    ItemData(bool isFolderItem, std::string itemId) : isFolder(isFolderItem), id(std::move(itemId)) {}
    bool isFolder;
    std::string id;
};

Sidebar::Sidebar(wxWindow* parent, SidebarHost& host, Workspace& workspace)
    : wxPanel(parent), host_(host), workspace_(workspace) {
    const int gap = FromDIP(8);
    tabs_ = new TabbedBook(this, wxID_HIGHEST + 104);

    auto* newRequestButton = new FlatButton(tabs_, wxString(), FlatButton::Kind::Ghost, Glyph::Plus);
    newRequestButton->SetToolTip("New request");
    auto* newFolderButton = new FlatButton(tabs_, wxString(), FlatButton::Kind::Ghost, Glyph::FolderPlus);
    newFolderButton->SetToolTip("New folder");
    tabs_->headerSizer()->Add(newRequestButton, wxSizerFlags().CenterVertical());
    tabs_->headerSizer()->Add(newFolderButton, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(4)));

    // Saved requests.
    auto* savedPage = new wxPanel(tabs_->pageParent());
    filter_ = new Field(savedPage, wxID_ANY, 0, Glyph::Search);
    filter_->text()->SetHint("Filter");
    tree_ = new wxTreeCtrl(savedPage, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_NO_LINES | wxTR_FULL_ROW_HIGHLIGHT |
                               wxBORDER_NONE);
    tree_->SetIndent(FromDIP(14));
    savedHint_ = new MutedLabel(savedPage, wxString(), wxALIGN_CENTRE_HORIZONTAL);

    auto* savedSizer = new wxBoxSizer(wxVERTICAL);
    savedSizer->Add(filter_, wxSizerFlags().Expand().Border(wxALL, gap));
    savedSizer->Add(tree_, wxSizerFlags(1).Expand());
    savedSizer->Add(savedHint_, wxSizerFlags().Center().Border(wxALL, FromDIP(20)));
    savedPage->SetSizer(savedSizer);
    tabs_->addPage(savedPage, "Saved");

    // History.
    auto* historyPage = new wxPanel(tabs_->pageParent());
    historyList_ = new wxListCtrl(historyPage, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                  wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER | wxBORDER_NONE);
    historyList_->AppendColumn(wxString());
    auto* clearButton = new FlatButton(historyPage, "Clear history", FlatButton::Kind::Ghost);
    historyHint_ = new MutedLabel(historyPage, "Requests you send appear here.", wxALIGN_CENTRE_HORIZONTAL);
    auto* historySizer = new wxBoxSizer(wxVERTICAL);
    historySizer->Add(clearButton, wxSizerFlags().Right().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(4)));
    historySizer->AddSpacer(FromDIP(4));
    historySizer->Add(historyList_, wxSizerFlags(1).Expand());
    historySizer->Add(historyHint_, wxSizerFlags().Center().Border(wxALL, FromDIP(20)));
    historyPage->SetSizer(historySizer);
    tabs_->addPage(historyPage, "History");

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(tabs_, wxSizerFlags(1).Expand());
    SetSizer(root);

    newRequestButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { host_.newRequest(); });
    newFolderButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { newFolder(); });
    filter_->text()->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { refreshTree(); });

    tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& event) {
        const ItemData* data = dataOf(event.GetItem());
        if (data && !data->isFolder) {
            host_.openSavedRequest(data->id);
        } else {
            event.Skip();  // lets the tree expand or collapse the folder
        }
    });
    tree_->Bind(wxEVT_TREE_ITEM_MENU, [this](wxTreeEvent& event) { showItemMenu(event.GetItem(), event.GetPoint()); });
    tree_->Bind(wxEVT_TREE_KEY_DOWN, [this](wxTreeEvent& event) {
        const wxTreeItemId item = tree_->GetSelection();
        if (item.IsOk() && event.GetKeyCode() == WXK_F2) {
            renameItem(item);
        } else if (item.IsOk() && event.GetKeyCode() == WXK_DELETE) {
            deleteItem(item);
        } else {
            event.Skip();
        }
    });
    tree_->Bind(wxEVT_TREE_ITEM_COLLAPSED, [this](wxTreeEvent& event) {
        const ItemData* data = dataOf(event.GetItem());
        if (data && !rebuilding_) collapsedFolders_.insert(data->id);
    });
    tree_->Bind(wxEVT_TREE_ITEM_EXPANDED, [this](wxTreeEvent& event) {
        const ItemData* data = dataOf(event.GetItem());
        if (data && !rebuilding_) collapsedFolders_.erase(data->id);
    });
    tree_->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& event) {
        dragged_ = event.GetItem();
        event.Allow();
    });
    tree_->Bind(wxEVT_TREE_END_DRAG, [this](wxTreeEvent& event) {
        // Letting go outside the tree cancels the move.
        if (tree_->GetClientRect().Contains(event.GetPoint())) {
            dropOn(event.GetItem());
        } else {
            dragged_ = wxTreeItemId();
        }
    });

    historyList_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
        const auto index = static_cast<std::size_t>(event.GetIndex());
        if (index < history_.size()) host_.openDraft(history_[index].request);
    });
    historyList_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        fitHistoryColumn();
        event.Skip();
    });
    clearButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { host_.clearHistory(); });

    refreshTree();
    showHistory({});
}

void Sidebar::applyTheme() {
    SetBackgroundColour(palette().sidebar);
    // The tags are drawn in theme colours, so they need redrawing.
    refreshTree();
    showHistory(history_);
}

void Sidebar::rebuildImages() {
    std::set<std::string> methods;
    collectMethods(workspace_.root(), methods);
    for (const auto& entry : history_) methods.insert(entry.request.method);

    wxVector<wxBitmapBundle> images;
    images.push_back(folderTagBundle());
    methodImages_.clear();
    for (const auto& method : methods) {
        const std::string key = upperCase(method);
        if (methodImages_.count(key)) continue;
        methodImages_[key] = static_cast<int>(images.size());
        images.push_back(methodTagBundle(key));
    }
    tree_->SetImages(images);
    historyList_->SetSmallImages(images);
}

int Sidebar::imageForMethod(const std::string& method) const {
    const auto it = methodImages_.find(upperCase(method));
    return it == methodImages_.end() ? -1 : it->second;
}

void Sidebar::refreshTree() {
    std::string selected;
    if (const ItemData* data = dataOf(tree_->GetSelection())) selected = data->id;

    rebuildImages();
    rebuilding_ = true;
    tree_->Freeze();
    tree_->DeleteAllItems();
    const wxTreeItemId root = tree_->AddRoot(wxString());
    const bool filtering = !filter_->text()->GetValue().empty();
    addFolderItems(root, workspace_.root(), filtering);
    tree_->Thaw();
    rebuilding_ = false;

    if (!selected.empty()) selectItem(selected);

    const bool empty = tree_->GetChildrenCount(root, false) == 0;
    savedHint_->SetLabel(filtering ? wxString("Nothing matches.")
                                   : wxString::Format("No saved requests yet.\nPress %s+S to save one.", controlKeyName()));
    tree_->Show(!empty);
    savedHint_->Show(empty);
    tree_->GetParent()->Layout();
}

void Sidebar::addFolderItems(const wxTreeItemId& parent, const Folder& folder, bool expandAll) {
    for (const auto& child : folder.folders) {
        if (!folderMatchesFilter(child)) continue;
        const wxTreeItemId item = tree_->AppendItem(parent, toWx(child.name), kFolderImage, -1,
                                                    new ItemData(true, child.id));
        addFolderItems(item, child, expandAll);
        if (expandAll || collapsedFolders_.count(child.id) == 0) tree_->Expand(item);
    }
    for (const auto& request : folder.requests) {
        if (!matchesFilter(request)) continue;
        tree_->AppendItem(parent, toWx(request.name), imageForMethod(request.method), -1,
                          new ItemData(false, request.id));
    }
}

bool Sidebar::matchesFilter(const Request& request) const {
    const wxString filter = filter_->text()->GetValue().Lower();
    if (filter.empty()) return true;
    return toWx(request.name).Lower().Contains(filter) || toWx(request.url).Lower().Contains(filter) ||
           toWx(request.method).Lower().Contains(filter);
}

bool Sidebar::folderMatchesFilter(const Folder& folder) const {
    const wxString filter = filter_->text()->GetValue().Lower();
    if (filter.empty() || toWx(folder.name).Lower().Contains(filter)) return true;
    return std::any_of(folder.requests.begin(), folder.requests.end(),
                       [&](const Request& r) { return matchesFilter(r); }) ||
           std::any_of(folder.folders.begin(), folder.folders.end(),
                       [&](const Folder& f) { return folderMatchesFilter(f); });
}

const Sidebar::ItemData* Sidebar::dataOf(const wxTreeItemId& item) const {
    if (!item.IsOk()) return nullptr;
    return static_cast<const ItemData*>(tree_->GetItemData(item));
}

void Sidebar::selectItem(const std::string& id) {
    // Walks the visible tree, since items are recreated on every refresh.
    std::vector<wxTreeItemId> pending{tree_->GetRootItem()};
    while (!pending.empty()) {
        const wxTreeItemId item = pending.back();
        pending.pop_back();
        if (!item.IsOk()) continue;
        const ItemData* data = dataOf(item);
        if (data && data->id == id) {
            tree_->SelectItem(item);
            tree_->EnsureVisible(item);
            return;
        }
        wxTreeItemIdValue cookie;
        for (wxTreeItemId child = tree_->GetFirstChild(item, cookie); child.IsOk();
             child = tree_->GetNextChild(item, cookie))
            pending.push_back(child);
    }
}

std::string Sidebar::targetFolderId() const {
    const ItemData* data = dataOf(tree_->GetSelection());
    if (!data) return Workspace::kRootId;
    if (data->isFolder) return data->id;
    const Folder* parent = workspace_.parentOfRequest(data->id);
    return parent ? parent->id : Workspace::kRootId;
}

void Sidebar::newFolder() {
    const wxString name = askForText(this, "New Folder", "Name of the new folder", "New folder");
    if (name.empty()) return;
    const std::string id = workspace_.addFolder(targetFolderId(), toUtf8(name));
    host_.workspaceChanged();
    refreshTree();
    selectItem(id);
}

void Sidebar::showItemMenu(const wxTreeItemId& item, const wxPoint& position) {
    const ItemData* data = dataOf(item);
    if (!data) return;
    tree_->SelectItem(item);

    enum { kOpen = wxID_HIGHEST + 200, kNewRequest, kNewFolder, kRename, kDuplicate, kDelete, kImport, kExport };
    const MenuEntry divider{wxID_NONE, wxString(), wxString()};
    const MenuEntry rename{kRename, "Rename...", "F2"};
    const MenuEntry remove{kDelete, "Delete...", "Del", ColourRole::Danger};
    std::vector<MenuEntry> entries;
    if (data->isFolder) {
        entries = {{kNewRequest, "New Request", wxString()},
                   {kNewFolder, "New Folder...", wxString()},
                   divider,
                   rename,
                   remove,
                   divider,
                   {kImport, "Import Collection Here...", wxString()},
                   {kExport, "Export Folder...", wxString()}};
    } else {
        entries = {{kOpen, "Open", wxString()}, divider, rename, {kDuplicate, "Duplicate", wxString()}, remove};
    }

    const std::string id = data->id;
    showContextMenu(tree_, entries, tree_->ClientToScreen(position), [this, item, id](int choice) {
        switch (choice) {
            case kOpen: host_.openSavedRequest(id); break;
            case kNewRequest: host_.newRequest(); break;
            case kNewFolder: newFolder(); break;
            case kRename: renameItem(item); break;
            case kDuplicate: duplicateItem(item); break;
            case kDelete: deleteItem(item); break;
            case kImport: host_.importCollection(); break;
            case kExport: host_.exportCollection(id); break;
            default: break;
        }
    });
}

void Sidebar::renameItem(const wxTreeItemId& item) {
    const ItemData* data = dataOf(item);
    if (!data) return;
    const std::string id = data->id;
    const bool isFolder = data->isFolder;

    std::string current;
    if (isFolder) {
        if (const Folder* folder = workspace_.findFolder(id)) current = folder->name;
    } else if (const Request* request = workspace_.findRequest(id)) {
        current = request->name;
    }

    const wxString name = askForText(this, "Rename", "New name", toWx(current));
    if (name.empty() || toUtf8(name) == current) return;

    if (isFolder) {
        workspace_.renameFolder(id, toUtf8(name));
    } else if (Request* request = workspace_.findRequest(id)) {
        request->name = toUtf8(name);
        host_.savedRequestRenamed(id, request->name);
    }
    host_.workspaceChanged();
    refreshTree();
}

void Sidebar::deleteItem(const wxTreeItemId& item) {
    const ItemData* data = dataOf(item);
    if (!data) return;
    const std::string id = data->id;

    std::vector<std::string> removedRequests;
    wxString question;
    if (data->isFolder) {
        const Folder* folder = workspace_.findFolder(id);
        if (!folder) return;
        collectRequestIds(*folder, removedRequests);
        question = wxString::Format("Delete the folder \"%s\" and the %zu saved request(s) in it?",
                                    toWx(folder->name), removedRequests.size());
    } else {
        const Request* request = workspace_.findRequest(id);
        if (!request) return;
        removedRequests.push_back(id);
        question = wxString::Format("Delete the saved request \"%s\"?", toWx(request->name));
    }

    if (!confirmDelete(this, "Delete", question)) return;

    const bool isFolder = data->isFolder;
    if (isFolder) {
        workspace_.removeFolder(id);
        collapsedFolders_.erase(id);
    } else {
        workspace_.removeRequest(id);
    }
    host_.savedRequestsRemoved(removedRequests);
    host_.workspaceChanged();
    refreshTree();
}

void Sidebar::duplicateItem(const wxTreeItemId& item) {
    const ItemData* data = dataOf(item);
    if (!data || data->isFolder) return;
    const std::string copy = workspace_.duplicateRequest(data->id);
    host_.workspaceChanged();
    refreshTree();
    selectItem(copy);
}

void Sidebar::dropOn(const wxTreeItemId& target) {
    const ItemData* moved = dataOf(dragged_);
    dragged_ = wxTreeItemId();
    if (!moved) return;
    const std::string id = moved->id;
    const bool isFolder = moved->isFolder;

    // Dropping on a request means its folder; dropping on empty space, the root.
    std::string destination = Workspace::kRootId;
    if (const ItemData* over = dataOf(target)) {
        if (over->isFolder) {
            destination = over->id;
        } else if (const Folder* parent = workspace_.parentOfRequest(over->id)) {
            destination = parent->id;
        }
    }

    const bool ok = isFolder ? workspace_.moveFolder(id, destination) : workspace_.moveRequest(id, destination);
    if (!ok) return;
    host_.workspaceChanged();
    refreshTree();
    selectItem(id);
}

void Sidebar::showHistory(const std::vector<HistoryEntry>& history) {
    if (&history != &history_) history_ = history;
    rebuildImages();
    const Palette& colours = palette();

    historyList_->Freeze();
    historyList_->DeleteAllItems();
    long row = 0;
    for (const auto& entry : history_) {
        const wxString status = entry.status ? wxString::Format("%ld", entry.status) : wxString::FromUTF8("\xE2\x80\x94");
        historyList_->InsertItem(row, status + "  " + shortUrl(entry.request.url), imageForMethod(entry.request.method));
        if (entry.status == 0 || entry.status >= 400) historyList_->SetItemTextColour(row, colours.danger);
        ++row;
    }
    historyList_->Thaw();
    fitHistoryColumn();

    const bool empty = history_.empty();
    historyList_->Show(!empty);
    historyHint_->Show(empty);
    historyList_->GetParent()->Layout();
}

void Sidebar::fitHistoryColumn() {
    historyList_->SetColumnWidth(0, std::max(FromDIP(80), historyList_->GetClientSize().x));
}

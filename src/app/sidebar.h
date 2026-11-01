// The left pane: saved requests in folders, plus the history of sent requests.
#pragma once

#include "core/model.h"
#include "core/workspace.h"
#include "theme.h"
#include "wx_util.h"

#include <wx/panel.h>
#include <wx/treectrl.h>

#include <map>
#include <set>
#include <string>
#include <vector>

class Field;
class MutedLabel;
class TabbedBook;
class wxListCtrl;
class wxListEvent;

class SidebarHost {
public:
    virtual ~SidebarHost() = default;
    virtual void openSavedRequest(const std::string& id) = 0;
    virtual void openDraft(const Request& request) = 0;
    virtual void newRequest() = 0;
    virtual void savedRequestRenamed(const std::string& id, const std::string& name) = 0;
    virtual void savedRequestsRemoved(const std::vector<std::string>& ids) = 0;
    // The workspace changed and should be written to disk.
    virtual void workspaceChanged() = 0;
    virtual void importCollection() = 0;
    virtual void exportCollection(const std::string& folderId) = 0;
    virtual void clearHistory() = 0;
};

class Sidebar : public wxPanel, public ThemeAware {
public:
    Sidebar(wxWindow* parent, SidebarHost& host, Workspace& workspace);

    void refreshTree();
    void showHistory(const std::vector<HistoryEntry>& history);
    void selectItem(const std::string& id);

    // The selected folder, or the folder holding the selected request, or the
    // root. New items go here.
    std::string targetFolderId() const;
    // Asks for a name and adds a folder under targetFolderId().
    void newFolder();

    void applyTheme() override;

private:
    struct ItemData;

    void rebuildImages();
    int imageForMethod(const std::string& method) const;
    void addFolderItems(const wxTreeItemId& parent, const Folder& folder, bool expandAll);
    bool matchesFilter(const Request& request) const;
    bool folderMatchesFilter(const Folder& folder) const;
    const ItemData* dataOf(const wxTreeItemId& item) const;
    void fitHistoryColumn();

    void showItemMenu(const wxTreeItemId& item, const wxPoint& position);
    void renameItem(const wxTreeItemId& item);
    void deleteItem(const wxTreeItemId& item);
    void duplicateItem(const wxTreeItemId& item);
    void dropOn(const wxTreeItemId& target);

    SidebarHost& host_;
    Workspace& workspace_;
    TabbedBook* tabs_;
    Field* filter_;
    wxTreeCtrl* tree_;
    MutedLabel* savedHint_;
    wxListCtrl* historyList_;
    MutedLabel* historyHint_;
    std::vector<HistoryEntry> history_;
    std::map<std::string, int> methodImages_;
    std::set<std::string> collapsedFolders_;
    wxTreeItemId dragged_;
    bool rebuilding_ = false;
};

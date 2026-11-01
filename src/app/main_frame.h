// The main window. Owns the saved data and connects the sidebar to the tabs.
#pragma once

#include "core/model.h"
#include "core/store.h"
#include "core/workspace.h"
#include "request_panel.h"
#include "sidebar.h"
#include "theme.h"

#include <wx/frame.h>

#include <filesystem>
#include <string>
#include <vector>

class DocumentTabs;
class FlatButton;
class ThinSplitter;

class MainFrame : public wxFrame, private RequestPanelHost, private SidebarHost {
public:
    explicit MainFrame(const std::filesystem::path& dataDirectory);

private:
    // RequestPanelHost
    void requestEdited(RequestPanel* panel) override;
    void exchangeFinished(const Request& request, const HttpResponse& response) override;

    // SidebarHost
    void openSavedRequest(const std::string& id) override;
    void openDraft(const Request& request) override;
    void newRequest() override;
    void savedRequestRenamed(const std::string& id, const std::string& name) override;
    void savedRequestsRemoved(const std::vector<std::string>& ids) override;
    void workspaceChanged() override;
    void importCollection() override;
    void exportCollection(const std::string& folderId) override;
    void clearHistory() override;

    void buildMenus();
    void loadData();
    RequestPanel* currentPanel() const;
    RequestPanel* panelAt(std::size_t index) const;
    RequestPanel* panelForSavedRequest(const std::string& id) const;
    RequestPanel* openTab(const Request& request);
    void closeTabAt(std::size_t index);

    // Returns false if the user cancelled or the save failed.
    bool saveTab(RequestPanel* panel, bool askForLocation);
    // Saves or discards unsaved changes as the user chooses. False means cancel.
    bool confirmClose(RequestPanel* panel);
    bool writeWorkspace();
    void writeHistory();
    void forwardEditCommand(wxCommandEvent& event);
    void onClose(wxCloseEvent& event);

    void chooseTheme(ThemeMode mode);
    void updateThemeControls();

    Store store_;
    Workspace workspace_;
    std::vector<HistoryEntry> history_;

    ThinSplitter* split_;
    Sidebar* sidebar_;
    DocumentTabs* tabs_;
    FlatButton* themeButton_;
};

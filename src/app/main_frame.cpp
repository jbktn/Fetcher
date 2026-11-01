#include "main_frame.h"

#include "core/serialize.h"
#include "core/url.h"
#include "dialogs.h"
#include "document_tabs.h"
#include "fonts.h"
#include "widgets.h"
#include "wx_util.h"

#include <wx/config.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/mstream.h>
#include <wx/persist/toplevel.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/textentry.h>

#include <chrono>

#ifdef __WXGTK__
#include "app_icon_png.h"
#endif

namespace {

enum {
    ID_NEW_REQUEST = wxID_HIGHEST + 300,
    ID_NEW_FOLDER,
    ID_SAVE_AS,
    ID_CLOSE_TAB,
    ID_IMPORT,
    ID_EXPORT,
    ID_SEND,
    ID_FORMAT,
    ID_DUPLICATE_TAB,
    ID_FOCUS_URL,
    ID_NEXT_TAB,
    ID_PREVIOUS_TAB,
    ID_THEME_SYSTEM,
    ID_THEME_LIGHT,
    ID_THEME_DARK,
    ID_THEME_TOGGLE,
};

constexpr const char* kSashConfigKey = "/Window/SidebarWidth";

// Lets the user name a request and pick the folder it is saved in.
class SaveRequestDialog : public ThemedDialog {
public:
    SaveRequestDialog(wxWindow* parent, const Folder& root, const std::string& initialName,
                      const std::string& initialFolder)
        : ThemedDialog(parent, "Save Request") {
        auto* nameField = new Field(this);
        nameField->SetMinSize(wxSize(FromDIP(340), FromDIP(34)));
        name_ = nameField->text();
        name_->SetValue(toWx(initialName));

        folder_ = new Dropdown(this);
        addFolder(root, wxString(), initialFolder);
        if (folder_->GetSelection() == wxNOT_FOUND) folder_->SetSelection(0);
        // The longest path shouldn't decide how wide the dialog is.
        folder_->SetMinSize(wxSize(FromDIP(340), FromDIP(34)));

        addButton("Cancel", FlatButton::Kind::Subtle, wxID_CANCEL);
        auto* save = addButton("Save", FlatButton::Kind::Primary, wxID_OK);
        setDefaultButton(save);

        auto* content = new wxBoxSizer(wxVERTICAL);
        content->Add(new MutedLabel(this, "Name"));
        content->AddSpacer(FromDIP(6));
        content->Add(nameField, wxSizerFlags().Expand());
        content->AddSpacer(FromDIP(14));
        content->Add(new MutedLabel(this, "Folder"));
        content->AddSpacer(FromDIP(6));
        content->Add(folder_, wxSizerFlags().Expand());
        finish(content);

        const auto update = [this, save] { save->Enable(!name().empty()); };
        update();
        name_->Bind(wxEVT_TEXT, [update](wxCommandEvent&) { update(); });
        // Ready to type a name straight away.
        name_->SetFocus();
        name_->SelectAll();
    }

    std::string name() const { return toUtf8(name_->GetValue().Strip(wxString::both)); }
    std::string folderId() const { return folderIds_[static_cast<std::size_t>(folder_->GetSelection())]; }

private:
    // Whole paths, because the list can't show nesting and names may repeat.
    void addFolder(const Folder& folder, const wxString& parentPath, const std::string& initial) {
        const wxString path = folderIds_.empty() ? wxString("Top level")
                              : parentPath.empty() ? toWx(folder.name)
                                                   : parentPath + " / " + toWx(folder.name);
        folder_->addItem(path);
        folderIds_.push_back(folder.id);
        if (folder.id == initial) folder_->SetSelection(static_cast<int>(folderIds_.size()) - 1);
        const wxString childPath = folderIds_.size() == 1 ? wxString() : path;
        for (const auto& child : folder.folders) addFolder(child, childPath, initial);
    }

    wxTextCtrl* name_;
    Dropdown* folder_;
    std::vector<std::string> folderIds_;
};

// The app's icon for the About dialog, where the platform can supply one.
wxBitmap aboutIcon(wxWindow* window) {
    const int size = window->FromDIP(48);
#if defined(__WXMSW__)
    wxBitmap bitmap;
    bitmap.CopyFromIcon(wxIcon("appicon", wxBITMAP_TYPE_ICO_RESOURCE, size, size));
    return bitmap;
#elif defined(__WXGTK__)
    wxMemoryInputStream stream(kAppIconPng, sizeof(kAppIconPng));
    wxImage image(stream, wxBITMAP_TYPE_PNG);
    return image.IsOk() ? wxBitmap(image.Rescale(size, size, wxIMAGE_QUALITY_HIGH)) : wxBitmap();
#else
    wxUnusedVar(size);
    return wxBitmap();
#endif
}

std::string suggestedName(const Request& request) {
    if (!request.name.empty()) return request.name;
    UrlParts parts;
    if (parseUrl(normalizeUrl(request.url), parts)) {
        // The method isn't included because the saved list already shows it.
        const std::string path = parts.path.substr(0, parts.path.find('?'));
        return path == "/" ? parts.host : path;
    }
    return "New request";
}

}  // namespace

MainFrame::MainFrame(const std::filesystem::path& dataDirectory)
    : wxFrame(nullptr, wxID_ANY, "Fetcher", wxDefaultPosition, wxDefaultSize), store_(dataDirectory) {
    SetFont(uiFont());
#if defined(__WXMSW__)
    SetIcon(wxICON(appicon));
#elif defined(__WXGTK__)
    wxMemoryInputStream iconStream(kAppIconPng, sizeof(kAppIconPng));
    wxImage iconImage(iconStream, wxBITMAP_TYPE_PNG);
    if (iconImage.IsOk()) {
        wxIcon icon;
        icon.CopyFromBitmap(wxBitmap(iconImage));
        SetIcon(icon);
    }
#endif

    SetMinClientSize(FromDIP(wxSize(760, 480)));

    split_ = new ThinSplitter(this);
    split_->SetMinimumPaneSize(FromDIP(180));

    sidebar_ = new Sidebar(split_, *this, workspace_);
    tabs_ = new DocumentTabs(split_);
    tabs_->onCloseRequested = [this](std::size_t index) { closeTabAt(index); };
    tabs_->onNewRequested = [this] { newRequest(); };

    themeButton_ = new FlatButton(tabs_, wxString(), FlatButton::Kind::Ghost, Glyph::Moon);
    tabs_->addTrailing(themeButton_);

    const int sidebarWidth = static_cast<int>(wxConfigBase::Get()->ReadLong(kSashConfigKey, FromDIP(260)));
    split_->SplitVertically(sidebar_, tabs_, sidebarWidth);

    buildMenus();
    loadData();
    openTab(Request{});

    themeButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        chooseTheme(isDarkTheme() ? ThemeMode::Light : ThemeMode::Dark);
    });
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        // The system switched while the app is following it.
        refreshTheme(this);
        updateThemeControls();
        event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);

    updateThemeControls();
    applyThemeTo(this);

    SetClientSize(FromDIP(wxSize(1240, 800)));
    CentreOnScreen();
    wxPersistentRegisterAndRestore(this, "MainWindow");
}

void MainFrame::buildMenus() {
    auto* file = new wxMenu;
    file->Append(ID_NEW_REQUEST, "&New Request\tCtrl+N");
    file->Append(ID_NEW_FOLDER, "New &Folder...");
    file->AppendSeparator();
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
    file->Append(ID_SAVE_AS, "Save &As...\tCtrl+Shift+S");
    file->AppendSeparator();
    file->Append(ID_CLOSE_TAB, "&Close Tab\tCtrl+W");
    file->AppendSeparator();
    file->Append(ID_IMPORT, "&Import Collection...");
    file->Append(ID_EXPORT, "&Export Collection...");
    file->AppendSeparator();
    file->Append(wxID_EXIT);

    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO);
    edit->Append(wxID_REDO);
    edit->AppendSeparator();
    edit->Append(wxID_CUT);
    edit->Append(wxID_COPY);
    edit->Append(wxID_PASTE);
    edit->AppendSeparator();
    edit->Append(wxID_SELECTALL);

    auto* appearance = new wxMenu;
    appearance->AppendRadioItem(ID_THEME_SYSTEM, "&System");
    appearance->AppendRadioItem(ID_THEME_LIGHT, "&Light");
    appearance->AppendRadioItem(ID_THEME_DARK, "&Dark");

    auto* view = new wxMenu;
    view->AppendSubMenu(appearance, "&Appearance");
    view->Append(ID_THEME_TOGGLE, "&Toggle Light/Dark\tCtrl+Shift+L");
    view->AppendSeparator();
    view->Append(ID_NEXT_TAB, "&Next Tab\tCtrl+PgDn");
    view->Append(ID_PREVIOUS_TAB, "&Previous Tab\tCtrl+PgUp");

    auto* request = new wxMenu;
    request->Append(ID_SEND, "&Send or Cancel\tCtrl+Enter");
    request->Append(ID_FOCUS_URL, "Go to &URL\tCtrl+L");
    request->Append(ID_FORMAT, "&Format JSON Body\tCtrl+Shift+F");
    request->Append(ID_DUPLICATE_TAB, "&Duplicate Tab\tCtrl+D");

    auto* help = new wxMenu;
    help->Append(wxID_ABOUT);

    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(edit, "&Edit");
    bar->Append(view, "&View");
    bar->Append(request, "&Request");
    bar->Append(help, "&Help");
    SetMenuBar(bar);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { newRequest(); }, ID_NEW_REQUEST);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { sidebar_->newFolder(); }, ID_NEW_FOLDER);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (auto* panel = currentPanel()) saveTab(panel, false);
    }, wxID_SAVE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (auto* panel = currentPanel()) saveTab(panel, true);
    }, ID_SAVE_AS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        const int selection = tabs_->selection();
        if (selection != wxNOT_FOUND) closeTabAt(static_cast<std::size_t>(selection));
    }, ID_CLOSE_TAB);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { importCollection(); }, ID_IMPORT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { exportCollection(sidebar_->targetFolderId()); }, ID_EXPORT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { chooseTheme(ThemeMode::System); }, ID_THEME_SYSTEM);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { chooseTheme(ThemeMode::Light); }, ID_THEME_LIGHT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { chooseTheme(ThemeMode::Dark); }, ID_THEME_DARK);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        chooseTheme(isDarkTheme() ? ThemeMode::Light : ThemeMode::Dark);
    }, ID_THEME_TOGGLE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { tabs_->selectRelative(1); }, ID_NEXT_TAB);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { tabs_->selectRelative(-1); }, ID_PREVIOUS_TAB);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (auto* panel = currentPanel()) panel->toggleSend();
    }, ID_SEND);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (auto* panel = currentPanel()) panel->focusUrl();
    }, ID_FOCUS_URL);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (auto* panel = currentPanel()) panel->formatBody();
    }, ID_FORMAT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (auto* panel = currentPanel()) {
            Request copy = panel->currentRequest();
            copy.id.clear();
            if (!copy.name.empty()) copy.name += " copy";
            // Nothing on disk backs the copy, so closing it should ask first.
            openTab(copy)->detachFromCollection();
        }
    }, ID_DUPLICATE_TAB);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { showAbout(this, aboutIcon(this)); }, wxID_ABOUT);
    for (int id : {wxID_UNDO, wxID_REDO, wxID_CUT, wxID_COPY, wxID_PASTE, wxID_SELECTALL})
        Bind(wxEVT_MENU, &MainFrame::forwardEditCommand, this, id);
}

void MainFrame::loadData() {
    const std::string workspaceProblem = store_.load(workspace_);
    if (!workspaceProblem.empty()) showNotice(this, "Fetcher", toWx(workspaceProblem));
    const std::string historyProblem = store_.load(history_);
    if (!historyProblem.empty()) showNotice(this, "Fetcher", toWx(historyProblem));
    sidebar_->refreshTree();
    sidebar_->showHistory(history_);
}

void MainFrame::chooseTheme(ThemeMode mode) {
    setThemeMode(mode, this);
    updateThemeControls();
}

void MainFrame::updateThemeControls() {
    const bool dark = isDarkTheme();
    themeButton_->setGlyph(dark ? Glyph::Sun : Glyph::Moon);
    themeButton_->SetToolTip(dark ? "Switch to light mode" : "Switch to dark mode");

    const ThemeMode mode = themeMode();
    GetMenuBar()->Check(mode == ThemeMode::Light  ? ID_THEME_LIGHT
                        : mode == ThemeMode::Dark ? ID_THEME_DARK
                                                  : ID_THEME_SYSTEM,
                        true);
}

RequestPanel* MainFrame::panelAt(std::size_t index) const {
    return index < tabs_->pageCount() ? static_cast<RequestPanel*>(tabs_->page(index)) : nullptr;
}

RequestPanel* MainFrame::currentPanel() const {
    const int selection = tabs_->selection();
    return selection == wxNOT_FOUND ? nullptr : panelAt(static_cast<std::size_t>(selection));
}

RequestPanel* MainFrame::panelForSavedRequest(const std::string& id) const {
    for (std::size_t i = 0; i < tabs_->pageCount(); ++i)
        if (panelAt(i)->savedId() == id) return panelAt(i);
    return nullptr;
}

RequestPanel* MainFrame::openTab(const Request& request) {
    // Replace an untouched tab instead of leaving it behind.
    RequestPanel* blank = currentPanel();
    if (blank && !blank->isBlank()) blank = nullptr;

    auto* panel = new RequestPanel(tabs_->pageParent(), *this, request);
    applyThemeTo(panel);
    tabs_->addPage(panel, panel->title(), panel->isDirty());
    if (blank) tabs_->removePage(blank);
    panel->focusUrl();
    return panel;
}

void MainFrame::closeTabAt(std::size_t index) {
    auto* panel = panelAt(index);
    if (!panel || !confirmClose(panel)) return;
    tabs_->removePage(panel);
    if (tabs_->pageCount() == 0) openTab(Request{});
}

bool MainFrame::saveTab(RequestPanel* panel, bool askForLocation) {
    Request request = panel->currentRequest();

    if (askForLocation || request.id.empty()) {
        std::string folder = sidebar_->targetFolderId();
        if (const Folder* parent = workspace_.parentOfRequest(request.id)) folder = parent->id;
        SaveRequestDialog dialog(this, workspace_.root(), suggestedName(request), folder);
        if (dialog.ShowModal() != wxID_OK) return false;
        request.name = dialog.name();
        request.id = workspace_.addRequest(dialog.folderId(), request);
        if (!writeWorkspace()) {
            workspace_.removeRequest(request.id);
            return false;
        }
    } else {
        const Request previous = *workspace_.findRequest(request.id);
        workspace_.updateRequest(request);
        if (!writeWorkspace()) {
            workspace_.updateRequest(previous);
            return false;
        }
    }

    panel->markSaved(request);
    sidebar_->refreshTree();
    sidebar_->selectItem(request.id);
    return true;
}

bool MainFrame::confirmClose(RequestPanel* panel) {
    if (!panel->isDirty()) return true;

    const Request request = panel->currentRequest();
    const wxString name = request.name.empty() ? wxString("this new request") : "\"" + toWx(request.name) + "\"";
    switch (askToSave(this, wxString::Format("Save your changes to %s?", name))) {
        case SaveAnswer::Save: return saveTab(panel, false);
        case SaveAnswer::DontSave: return true;
        case SaveAnswer::Cancel: return false;
    }
    return false;
}

bool MainFrame::writeWorkspace() {
    try {
        store_.save(workspace_);
        return true;
    } catch (const std::exception& e) {
        showNotice(this, "Could not save your requests", toWx(e.what()));
        return false;
    }
}

void MainFrame::writeHistory() {
    try {
        store_.save(history_);
    } catch (const std::exception&) {
        // History is a convenience; failing to record it must not interrupt
        // testing, and a dialog after every send would do exactly that.
    }
}

void MainFrame::requestEdited(RequestPanel* panel) {
    tabs_->setTitle(panel, panel->title(), panel->isDirty());
}

void MainFrame::exchangeFinished(const Request& request, const HttpResponse& response) {
    if (response.cancelled) return;

    HistoryEntry entry;
    entry.request = request;
    entry.status = response.status;
    entry.error = response.error;
    entry.totalMs = response.totalMs;
    entry.sentAt = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
    history_.insert(history_.begin(), std::move(entry));
    if (history_.size() > Store::kMaxHistory) history_.resize(Store::kMaxHistory);
    writeHistory();
    sidebar_->showHistory(history_);
}

void MainFrame::openSavedRequest(const std::string& id) {
    if (RequestPanel* open = panelForSavedRequest(id)) {
        tabs_->select(static_cast<std::size_t>(tabs_->indexOf(open)));
        return;
    }
    if (const Request* request = workspace_.findRequest(id)) openTab(*request);
}

void MainFrame::openDraft(const Request& request) {
    Request draft = request;
    draft.id.clear();
    draft.name.clear();
    openTab(draft);
}

void MainFrame::newRequest() {
    openTab(Request{});
}

void MainFrame::savedRequestRenamed(const std::string& id, const std::string& name) {
    if (RequestPanel* panel = panelForSavedRequest(id)) panel->setName(name);
}

void MainFrame::savedRequestsRemoved(const std::vector<std::string>& ids) {
    for (const auto& id : ids) {
        RequestPanel* panel = panelForSavedRequest(id);
        if (!panel) continue;
        if (panel->isDirty()) {
            panel->detachFromCollection();  // keep the unsaved edits
        } else {
            tabs_->removePage(panel);
        }
    }
    if (tabs_->pageCount() == 0) openTab(Request{});
}

void MainFrame::workspaceChanged() {
    writeWorkspace();
}

void MainFrame::importCollection() {
    wxFileDialog dialog(this, "Import collection", wxString(), wxString(), "Fetcher collections (*.json)|*.json",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return;
    try {
        Folder imported = folderFromJson(readFile(toPath(dialog.GetPath())));
        // A whole-workspace export has no name of its own.
        if (imported.name.empty()) imported.name = toUtf8(wxFileName(dialog.GetPath()).GetName());
        workspace_.importFolder(sidebar_->targetFolderId(), std::move(imported));
    } catch (const std::exception& e) {
        showNotice(this, "Could not import this file", toWx(e.what()));
        return;
    }
    writeWorkspace();
    sidebar_->refreshTree();
}

void MainFrame::exportCollection(const std::string& folderId) {
    const Folder* folder = workspace_.findFolder(folderId);
    if (!folder) return;
    const wxString defaultName = folder->name.empty() ? wxString("collection") : toWx(folder->name);
    wxFileDialog dialog(this, "Export collection (includes any saved credentials)", wxString(),
                        defaultName + ".json", "Fetcher collections (*.json)|*.json",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;
    try {
        writeFileAtomic(toPath(dialog.GetPath()), folderToJson(*folder));
    } catch (const std::exception& e) {
        showNotice(this, "Could not export the collection", toWx(e.what()));
    }
}

void MainFrame::clearHistory() {
    if (history_.empty()) return;
    if (!confirmDelete(this, "Clear History", "Delete all history entries?")) return;
    history_.clear();
    writeHistory();
    sidebar_->showHistory(history_);
}

void MainFrame::forwardEditCommand(wxCommandEvent& event) {
    // A menu shortcut such as Ctrl+C reaches the frame, not the focused control.
    auto* entry = dynamic_cast<wxTextEntryBase*>(wxWindow::FindFocus());
    if (!entry) {
        event.Skip();
        return;
    }
    switch (event.GetId()) {
        case wxID_UNDO: entry->Undo(); break;
        case wxID_REDO: entry->Redo(); break;
        case wxID_CUT: entry->Cut(); break;
        case wxID_COPY: entry->Copy(); break;
        case wxID_PASTE: entry->Paste(); break;
        case wxID_SELECTALL: entry->SelectAll(); break;
        default: break;
    }
}

void MainFrame::onClose(wxCloseEvent& event) {
    for (std::size_t i = 0; i < tabs_->pageCount(); ++i) {
        RequestPanel* panel = panelAt(i);
        if (!panel->isDirty()) continue;
        tabs_->select(i);
        if (!confirmClose(panel) && event.CanVeto()) {
            event.Veto();
            return;
        }
    }
    wxConfigBase::Get()->Write(kSashConfigKey, split_->GetSashPosition());
    Destroy();
}

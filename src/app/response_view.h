// The lower half of a request tab: the response body, its headers and the
// request as it was actually sent. Until a response arrives, it shows a short
// centred message instead.
#pragma once

#include "core/http_client.h"
#include "theme.h"
#include "wx_util.h"

#include <wx/panel.h>

class CodeEditor;
class Dropdown;
class Field;
class FlatCheck;
class MutedLabel;
class PageBook;
class Pill;
class RoleLabel;
class TabbedBook;
class wxActivityIndicator;

class ResponseView : public wxPanel {
public:
    explicit ResponseView(wxWindow* parent);

    void showIdle();
    void showBusy();
    void showResponse(HttpResponse response);
    void showError(const wxString& message);

private:
    void showMessage(const wxString& title, const wxString& detail, ColourRole titleRole, bool busy);
    void renderBody();
    void saveBody();

    PageBook* pages_;

    wxActivityIndicator* spinner_;
    RoleLabel* messageTitle_;
    MutedLabel* messageDetail_;

    TabbedBook* tabs_;
    Pill* status_;
    MutedLabel* details_;
    Dropdown* view_;
    FlatCheck* wrap_;
    Field* search_;
    CodeEditor* body_;
    CodeEditor* headers_;
    CodeEditor* sent_;

    HttpResponse response_;
};

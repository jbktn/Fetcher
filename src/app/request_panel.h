// One open request: the editor on top and its latest response below. Each tab
// in the main window holds one of these, and each can have a call in flight
// independently of the others.
#pragma once

#include "core/http_client.h"
#include "core/model.h"
#include "wx_util.h"

#include <wx/panel.h>

#include <atomic>
#include <memory>
#include <thread>

class CodeEditor;
class Dropdown;
class Field;
class FlatButton;
class FlatCheck;
class KeyValueEditor;
class PageBook;
class RequestPanel;
class ResponseView;
class TabbedBook;
class wxTextCtrl;

class RequestPanelHost {
public:
    virtual ~RequestPanelHost() = default;
    virtual void requestEdited(RequestPanel* panel) = 0;
    virtual void exchangeFinished(const Request& request, const HttpResponse& response) = 0;
};

class RequestPanel : public wxPanel {
public:
    RequestPanel(wxWindow* parent, RequestPanelHost& host, const Request& initial);
    ~RequestPanel() override;

    // The editor's contents as a Request, keeping the saved id and name.
    Request currentRequest() const;

    const std::string& savedId() const { return baseline_.id; }
    bool isDirty() const { return currentRequest() != baseline_; }
    // True for a fresh tab that has nothing worth keeping.
    bool isBlank() const;
    wxString title() const;

    // The current contents were just written to the collection as `saved`.
    void markSaved(const Request& saved);
    void setName(const std::string& name);
    // Treats the contents as unsaved work with no saved request behind them,
    // for example because the saved request was deleted.
    void detachFromCollection();

    void send();
    void cancelSend();
    void toggleSend();
    bool isSending() const { return job_ != nullptr; }
    void formatBody();
    void focusUrl();

private:
    struct Job {
        std::atomic<bool> cancel{false};
        std::thread thread;
    };

    void buildParamsPage();
    void buildHeadersPage();
    void buildBodyPage();
    void buildAuthPage();
    void buildOptionsPage();

    void load(const Request& request);
    void edited();
    void urlEdited();
    void paramsEdited();
    void methodChosen();
    void updateTabLabels();
    void updateBodyPage();
    void updateAuthPage();
    void finishSend(const Request& request, const std::shared_ptr<HttpResponse>& response);
    int timeoutSeconds() const;

    RequestPanelHost& host_;
    Request baseline_;  // the state "not dirty" is measured against
    bool loading_ = false;
    std::unique_ptr<Job> job_;
    wxString lastMethod_;

    Dropdown* method_;
    wxTextCtrl* url_;
    FlatButton* sendButton_;
    TabbedBook* tabs_;
    ResponseView* response_;

    KeyValueEditor* params_;
    KeyValueEditor* headers_;

    Dropdown* bodyType_;
    FlatButton* formatButton_;
    PageBook* bodyPages_;
    CodeEditor* body_;
    KeyValueEditor* form_;

    Dropdown* authType_;
    PageBook* authPages_;
    wxTextCtrl* bearerToken_;
    wxTextCtrl* basicUser_;
    wxTextCtrl* basicPassword_;
    wxTextCtrl* apiKeyName_;
    wxTextCtrl* apiKeyValue_;
    Dropdown* apiKeyLocation_;

    FlatCheck* followRedirects_;
    FlatCheck* verifyTls_;
    wxTextCtrl* timeout_;
};

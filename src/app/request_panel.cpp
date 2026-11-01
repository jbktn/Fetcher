#include "request_panel.h"

#include "code_editor.h"
#include "core/text.h"
#include "core/url.h"
#include "dialogs.h"
#include "key_value_editor.h"
#include "response_view.h"
#include "theme.h"
#include "widgets.h"

#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/valnum.h>

#include <algorithm>

namespace {

enum Tab { kParamsTab, kHeadersTab, kBodyTab, kAuthTab, kOptionsTab };
enum BodyPage { kNoBodyPage, kTextBodyPage, kFormBodyPage };

// Fixed window ids make the main controls easy for UI automation to find.
enum { ID_METHOD = wxID_HIGHEST + 100, ID_URL, ID_REQUEST_TABS };

const char* const kMethods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"};
const int kMethodCount = static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0]));

BodyType bodyTypeAt(int index) {
    switch (index) {
        case 1: return BodyType::Json;
        case 2: return BodyType::Xml;
        case 3: return BodyType::Text;
        case 4: return BodyType::Form;
        default: return BodyType::None;
    }
}

int indexOf(BodyType type) {
    switch (type) {
        case BodyType::Json: return 1;
        case BodyType::Xml: return 2;
        case BodyType::Text: return 3;
        case BodyType::Form: return 4;
        case BodyType::None: break;
    }
    return 0;
}

CodeLanguage languageOf(BodyType type) {
    switch (type) {
        case BodyType::Json: return CodeLanguage::Json;
        case BodyType::Xml: return CodeLanguage::Xml;
        default: return CodeLanguage::Plain;
    }
}

std::size_t enabledCount(const std::vector<KeyValue>& rows) {
    return static_cast<std::size_t>(
        std::count_if(rows.begin(), rows.end(), [](const KeyValue& row) { return row.enabled; }));
}

wxString countBadge(std::size_t count) {
    return count == 0 ? wxString() : wxString::Format("%zu", count);
}

wxString activeBadge(bool active) {
    return active ? wxString::FromUTF8("\xE2\x80\xA2") : wxString();
}

wxArrayString commonHeaderNames() {
    wxArrayString names;
    for (const char* name : {"Accept", "Accept-Encoding", "Accept-Language", "Authorization", "Cache-Control",
                             "Connection", "Content-Type", "Cookie", "If-Match", "If-Modified-Since",
                             "If-None-Match", "Origin", "Range", "Referer", "User-Agent", "X-API-Key",
                             "X-Requested-With"})
        names.Add(name);
    return names;
}

}  // namespace

RequestPanel::RequestPanel(wxWindow* parent, RequestPanelHost& host, const Request& initial)
    : wxPanel(parent), host_(host), baseline_(initial) {
    const int gap = FromDIP(12);

    auto* urlField = new Field(this, ID_URL, wxTE_PROCESS_ENTER);
    urlField->SetMinSize(wxSize(-1, FromDIP(38)));
    url_ = urlField->text();
    url_->SetHint("https://api.example.com/users");

    method_ = new Dropdown(urlField, Dropdown::Kind::Bare, ID_METHOD);
    method_->SetFont(method_->GetFont().Bold());
    for (const char* name : kMethods) method_->addItem(name);
    method_->addItem("Custom...");
    method_->setSteppableCount(kMethodCount);
    method_->SetInitialSize();
    method_->setTextColouring([](const wxString& text) { return methodColour(toUtf8(text)); });
    urlField->addLeading(method_);

    sendButton_ = new FlatButton(this, "Send", FlatButton::Kind::Primary);
    sendButton_->SetMinSize(wxSize(FromDIP(88), FromDIP(38)));

    auto* bar = new wxBoxSizer(wxHORIZONTAL);
    bar->Add(urlField, wxSizerFlags(1).CenterVertical().Border(wxRIGHT, FromDIP(8)));
    bar->Add(sendButton_, wxSizerFlags().CenterVertical());

    auto* split = new ThinSplitter(this);
    split->SetMinimumPaneSize(FromDIP(120));
    split->SetSashGravity(0.5);

    tabs_ = new TabbedBook(split, ID_REQUEST_TABS);
    buildParamsPage();
    buildHeadersPage();
    buildBodyPage();
    buildAuthPage();
    buildOptionsPage();

    response_ = new ResponseView(split);
    split->SplitHorizontally(tabs_, response_);  // starts halfway

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(bar, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
    root->AddSpacer(FromDIP(4));
    root->Add(split, wxSizerFlags(1).Expand());
    SetSizer(root);

    method_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { methodChosen(); });
    url_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { urlEdited(); });
    url_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { send(); });
    sendButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { toggleSend(); });

    load(initial);
    baseline_ = currentRequest();
    updateTabLabels();
}

RequestPanel::~RequestPanel() {
    if (job_) {
        job_->cancel = true;
        // The call notices cancellation quickly. Any completion it queued is
        // discarded along with this window's pending events.
        job_->thread.join();
    }
}

void RequestPanel::buildParamsPage() {
    params_ = new KeyValueEditor(tabs_->pageParent(), "Query parameter", "Value");
    params_->Bind(EVT_ROWS_CHANGED, [this](wxCommandEvent&) { paramsEdited(); });
    tabs_->addPage(params_, "Params");
}

void RequestPanel::buildHeadersPage() {
    headers_ = new KeyValueEditor(tabs_->pageParent(), "Header", "Value");
    headers_->setKeySuggestions(commonHeaderNames());
    headers_->Bind(EVT_ROWS_CHANGED, [this](wxCommandEvent&) { edited(); });
    tabs_->addPage(headers_, "Headers");
}

void RequestPanel::buildBodyPage() {
    const int gap = FromDIP(8);
    auto* page = new wxPanel(tabs_->pageParent());

    bodyType_ = new Dropdown(page);
    for (const char* name : {"No body", "JSON", "XML", "Text", "Form (URL-encoded)"}) bodyType_->addItem(name);
    formatButton_ = new FlatButton(page, "Format", FlatButton::Kind::Ghost);
    formatButton_->SetToolTip("Indent the JSON body");

    auto* bar = new wxBoxSizer(wxHORIZONTAL);
    bar->Add(bodyType_, wxSizerFlags().Center().Border(wxRIGHT, gap));
    bar->Add(formatButton_, wxSizerFlags().Center());

    bodyPages_ = new PageBook(page);
    auto* none = new wxPanel(bodyPages_);
    auto* noneSizer = new wxBoxSizer(wxVERTICAL);
    noneSizer->Add(new MutedLabel(none, "This request is sent without a body."),
                   wxSizerFlags().Border(wxLEFT | wxTOP, FromDIP(12)));
    none->SetSizer(noneSizer);
    bodyPages_->AddPage(none, wxString());

    body_ = new CodeEditor(bodyPages_, false);
    bodyPages_->AddPage(body_, wxString());

    form_ = new KeyValueEditor(bodyPages_, "Field", "Value");
    bodyPages_->AddPage(form_, wxString());

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(bar, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
    sizer->AddSpacer(FromDIP(6));
    sizer->Add(bodyPages_, wxSizerFlags(1).Expand());
    page->SetSizer(sizer);
    tabs_->addPage(page, "Body");

    bodyType_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        updateBodyPage();
        edited();
    });
    formatButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { formatBody(); });
    body_->Bind(wxEVT_STC_CHANGE, [this](wxStyledTextEvent& event) {
        edited();
        event.Skip();
    });
    form_->Bind(EVT_ROWS_CHANGED, [this](wxCommandEvent&) { edited(); });
}

void RequestPanel::buildAuthPage() {
    const int gap = FromDIP(10);
    auto* page = new wxPanel(tabs_->pageParent());

    authType_ = new Dropdown(page);
    for (const char* name : {"No authentication", "Bearer token", "Basic", "API key"}) authType_->addItem(name);

    authPages_ = new PageBook(page);

    const auto addLabel = [&](wxWindow* parent, wxFlexGridSizer* grid, const wxString& label) {
        grid->Add(new MutedLabel(parent, label), wxSizerFlags().CenterVertical());
    };
    const auto addField = [&](wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, long style = 0) {
        addLabel(parent, grid, label);
        auto* field = new Field(parent, wxID_ANY, style);
        grid->Add(field, wxSizerFlags().Expand());
        field->text()->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { edited(); });
        return field->text();
    };
    const auto newPage = [&](wxFlexGridSizer*& grid) {
        auto* fields = new wxPanel(authPages_);
        grid = new wxFlexGridSizer(2, gap, FromDIP(16));
        grid->AddGrowableCol(1);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(grid, wxSizerFlags().Expand().Border(wxALL, FromDIP(12)));
        fields->SetSizer(sizer);
        authPages_->AddPage(fields, wxString());
        return fields;
    };

    wxFlexGridSizer* grid = nullptr;
    newPage(grid);  // "No authentication" has no fields

    wxPanel* fields = newPage(grid);
    bearerToken_ = addField(fields, grid, "Token");

    fields = newPage(grid);
    basicUser_ = addField(fields, grid, "Username");
    basicPassword_ = addField(fields, grid, "Password", wxTE_PASSWORD);

    fields = newPage(grid);
    apiKeyName_ = addField(fields, grid, "Name");
    apiKeyValue_ = addField(fields, grid, "Value");
    addLabel(fields, grid, "Send in");
    apiKeyLocation_ = new Dropdown(fields);
    apiKeyLocation_->addItem("Header");
    apiKeyLocation_->addItem("Query string");
    grid->Add(apiKeyLocation_);
    apiKeyLocation_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { edited(); });

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(authType_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(8)));
    sizer->Add(authPages_, wxSizerFlags(1).Expand());
    page->SetSizer(sizer);
    tabs_->addPage(page, "Auth");

    authType_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        updateAuthPage();
        edited();
    });
}

void RequestPanel::buildOptionsPage() {
    const int gap = FromDIP(12);
    auto* page = new wxPanel(tabs_->pageParent());

    followRedirects_ = new FlatCheck(page, "Follow redirects");
    verifyTls_ = new FlatCheck(page, "Verify TLS certificates");

    auto* timeoutField = new Field(page);
    timeoutField->SetMinSize(FromDIP(wxSize(72, 32)));
    timeout_ = timeoutField->text();
    wxIntegerValidator<int> digitsOnly;
    digitsOnly.SetRange(0, 3600);
    timeout_->SetValidator(digitsOnly);

    auto* timeoutRow = new wxBoxSizer(wxHORIZONTAL);
    timeoutRow->Add(new MutedLabel(page, "Timeout"), wxSizerFlags().Center().Border(wxRIGHT, FromDIP(10)));
    timeoutRow->Add(timeoutField, wxSizerFlags().Center().Border(wxRIGHT, FromDIP(10)));
    timeoutRow->Add(new MutedLabel(page, "seconds (0 means no limit)"), wxSizerFlags().Center());

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(followRedirects_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, gap));
    sizer->Add(verifyTls_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, gap));
    sizer->Add(timeoutRow, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, gap));
    page->SetSizer(sizer);
    tabs_->addPage(page, "Options");

    followRedirects_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { edited(); });
    verifyTls_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { edited(); });
    timeout_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { edited(); });
}

void RequestPanel::load(const Request& request) {
    loading_ = true;
    method_->setText(toWx(request.method));
    lastMethod_ = toWx(request.method);
    url_->ChangeValue(toWx(request.url));
    params_->setRows(request.params);
    headers_->setRows(request.headers);

    bodyType_->SetSelection(indexOf(request.bodyType));
    body_->setContent(request.body);
    form_->setRows(request.form);
    updateBodyPage();

    authType_->SetSelection(static_cast<int>(request.auth.type));
    bearerToken_->ChangeValue(toWx(request.auth.token));
    basicUser_->ChangeValue(toWx(request.auth.username));
    basicPassword_->ChangeValue(toWx(request.auth.password));
    apiKeyName_->ChangeValue(toWx(request.auth.apiKeyName));
    apiKeyValue_->ChangeValue(toWx(request.auth.apiKeyValue));
    apiKeyLocation_->SetSelection(request.auth.apiKeyInQuery ? 1 : 0);
    updateAuthPage();

    followRedirects_->SetValue(request.options.followRedirects);
    verifyTls_->SetValue(request.options.verifyTls);
    timeout_->ChangeValue(wxString::Format("%d", request.options.timeoutSeconds));
    loading_ = false;
}

Request RequestPanel::currentRequest() const {
    Request request;
    request.id = baseline_.id;
    request.name = baseline_.name;
    request.method = toUtf8(method_->text());
    request.url = toUtf8(url_->GetValue());
    request.params = params_->rows();
    request.headers = headers_->rows();
    request.bodyType = bodyTypeAt(bodyType_->GetSelection());
    request.body = body_->content();
    request.form = form_->rows();
    request.auth.type = static_cast<AuthType>(authType_->GetSelection());
    request.auth.token = toUtf8(bearerToken_->GetValue());
    request.auth.username = toUtf8(basicUser_->GetValue());
    request.auth.password = toUtf8(basicPassword_->GetValue());
    request.auth.apiKeyName = toUtf8(apiKeyName_->GetValue());
    request.auth.apiKeyValue = toUtf8(apiKeyValue_->GetValue());
    request.auth.apiKeyInQuery = apiKeyLocation_->GetSelection() == 1;
    request.options.followRedirects = followRedirects_->GetValue();
    request.options.verifyTls = verifyTls_->GetValue();
    request.options.timeoutSeconds = timeoutSeconds();
    return request;
}

int RequestPanel::timeoutSeconds() const {
    long seconds = 0;
    if (!timeout_->GetValue().ToLong(&seconds) || seconds < 0) return baseline_.options.timeoutSeconds;
    return static_cast<int>(std::min(seconds, 3600L));
}

bool RequestPanel::isBlank() const {
    return baseline_.id.empty() && currentRequest() == Request{};
}

wxString RequestPanel::title() const {
    return baseline_.name.empty() ? wxString("Untitled") : toWx(baseline_.name);
}

void RequestPanel::markSaved(const Request& saved) {
    baseline_ = saved;
    host_.requestEdited(this);
}

void RequestPanel::setName(const std::string& name) {
    baseline_.name = name;
    host_.requestEdited(this);
}

void RequestPanel::detachFromCollection() {
    Request detached;
    detached.name = baseline_.name;
    baseline_ = detached;
    host_.requestEdited(this);
}

void RequestPanel::edited() {
    if (loading_) return;
    updateTabLabels();
    host_.requestEdited(this);
}

void RequestPanel::methodChosen() {
    if (method_->GetSelection() == kMethodCount) {
        // "Custom..." was picked, so ask what it should be.
        const wxString custom = askForText(this, "Custom Method", "HTTP method", lastMethod_).Upper();
        method_->setText(custom.empty() ? lastMethod_ : custom);
    }
    lastMethod_ = method_->text();
    edited();
}

void RequestPanel::urlEdited() {
    if (loading_) return;
    params_->setRows(mergeParams(toUtf8(url_->GetValue()), params_->rows()));
    edited();
}

void RequestPanel::paramsEdited() {
    if (loading_) return;
    url_->ChangeValue(toWx(withQuery(toUtf8(url_->GetValue()), params_->rows())));
    edited();
}

void RequestPanel::updateTabLabels() {
    tabs_->setBadge(kParamsTab, countBadge(enabledCount(params_->rows())));
    tabs_->setBadge(kHeadersTab, countBadge(enabledCount(headers_->rows())));
    tabs_->setBadge(kBodyTab, activeBadge(bodyTypeAt(bodyType_->GetSelection()) != BodyType::None));
    tabs_->setBadge(kAuthTab, activeBadge(authType_->GetSelection() != 0));
}

void RequestPanel::updateBodyPage() {
    const BodyType type = bodyTypeAt(bodyType_->GetSelection());
    formatButton_->Show(type == BodyType::Json);
    bodyPages_->ChangeSelection(type == BodyType::None   ? kNoBodyPage
                                : type == BodyType::Form ? kFormBodyPage
                                                         : kTextBodyPage);
    body_->setLanguage(languageOf(type));
    formatButton_->GetParent()->Layout();
}

void RequestPanel::updateAuthPage() {
    authPages_->ChangeSelection(static_cast<std::size_t>(authType_->GetSelection()));
}

void RequestPanel::send() {
    if (isSending()) return;

    Request request = currentRequest();
    PreparedRequest prepared = prepareRequest(request);
    if (prepared.url.empty()) {
        response_->showError("Enter a URL to send this request.");
        url_->SetFocus();
        return;
    }

    job_ = std::make_unique<Job>();
    sendButton_->setText("Cancel");
    response_->showBusy();

    job_->thread = std::thread([this, job = job_.get(), request = std::move(request),
                                prepared = std::move(prepared)] {
        auto response = std::make_shared<HttpResponse>();
        try {
            *response = sendRequest(prepared, job->cancel);
        } catch (const std::exception& e) {
            response->error = e.what();
        }
        CallAfter([this, request, response] { finishSend(request, response); });
    });
}

void RequestPanel::cancelSend() {
    if (job_) job_->cancel = true;
}

void RequestPanel::toggleSend() {
    if (isSending()) {
        cancelSend();
    } else {
        send();
    }
}

void RequestPanel::finishSend(const Request& request, const std::shared_ptr<HttpResponse>& response) {
    job_->thread.join();
    job_.reset();
    sendButton_->setText("Send");
    response_->showResponse(*response);
    host_.exchangeFinished(request, *response);
}

void RequestPanel::formatBody() {
    if (bodyTypeAt(bodyType_->GetSelection()) != BodyType::Json) return;
    if (auto formatted = prettyJson(body_->content())) {
        body_->setContent(*formatted);
    } else {
        showNotice(this, "Format JSON", "The body is not valid JSON, so it was left as it is.");
    }
}

void RequestPanel::focusUrl() {
    url_->SetFocus();
}

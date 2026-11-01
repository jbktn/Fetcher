#include "response_view.h"

#include "code_editor.h"
#include "core/store.h"
#include "core/text.h"
#include "dialogs.h"
#include "theme.h"
#include "widgets.h"

#include <wx/activityindicator.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/strconv.h>
#include <wx/textctrl.h>

namespace {

// Past these sizes, pretty printing, colouring and word wrap make the editor
// noticeably slow, so large responses are shown plainly.
constexpr std::size_t kMaxPrettyBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxColouredBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxWrappedBytes = 2 * 1024 * 1024;

enum Page { kMessagePage, kContentPage };

ColourRole statusRole(long status) {
    if (status >= 200 && status < 300) return ColourRole::Success;
    if (status >= 400 || status == 0) return ColourRole::Danger;
    return ColourRole::Warning;
}

CodeLanguage languageFor(const std::string& mime, std::string_view body) {
    if (mime.find("json") != std::string::npos) return CodeLanguage::Json;
    if (mime.find("html") != std::string::npos) return CodeLanguage::Html;
    if (mime.find("xml") != std::string::npos) return CodeLanguage::Xml;
    const auto first = body.find_first_not_of(" \t\r\n");
    if (first != std::string_view::npos && (body[first] == '{' || body[first] == '['))
        return CodeLanguage::Json;
    return CodeLanguage::Plain;
}

// Text in the response's declared charset, as UTF-8.
std::string decodeBody(const std::string& body, const std::string& contentType) {
    if (isValidUtf8(body)) return body;
    const std::string charset = charsetOf(contentType);
    if (!charset.empty()) {
        wxCSConv conversion(toWx(charset));
        if (conversion.IsOk()) {
            const wxString text(body.data(), conversion, body.size());
            if (!text.empty()) return toUtf8(text);
        }
    }
    return toUtf8(wxString::From8BitData(body.data(), body.size()));
}

}  // namespace

ResponseView::ResponseView(wxWindow* parent) : wxPanel(parent) {
    const int gap = FromDIP(8);
    pages_ = new PageBook(this);

    // A centred message for everything that isn't a response.
    auto* messagePage = new wxPanel(pages_);
    spinner_ = new wxActivityIndicator(messagePage);
    spinner_->Hide();
    messageTitle_ = new RoleLabel(messagePage, wxString(), ColourRole::Text, wxALIGN_CENTRE_HORIZONTAL);
    messageTitle_->SetFont(messageTitle_->GetFont().Bold());
    messageDetail_ = new MutedLabel(messagePage, wxString(), wxALIGN_CENTRE_HORIZONTAL);
    auto* messageColumn = new wxBoxSizer(wxVERTICAL);
    messageColumn->AddStretchSpacer();
    messageColumn->Add(spinner_, wxSizerFlags().Center().Border(wxBOTTOM, gap));
    messageColumn->Add(messageTitle_, wxSizerFlags().Center());
    messageColumn->Add(messageDetail_, wxSizerFlags().Center().Border(wxTOP, FromDIP(4)));
    messageColumn->AddStretchSpacer();
    messagePage->SetSizer(messageColumn);
    pages_->AddPage(messagePage, wxString());

    // The response itself, with its status at the right of the tabs.
    tabs_ = new TabbedBook(pages_, wxID_HIGHEST + 103);
    status_ = new Pill(tabs_);
    details_ = new MutedLabel(tabs_, wxString());
    tabs_->headerSizer()->Add(details_, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(12)));
    tabs_->headerSizer()->Add(status_, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(12)));

    auto* bodyPage = new wxPanel(tabs_->pageParent());
    view_ = new Dropdown(bodyPage);
    view_->addItem("Pretty");
    view_->addItem("Raw");
    wrap_ = new FlatCheck(bodyPage, "Wrap lines");
    search_ = new Field(bodyPage, wxID_ANY, wxTE_PROCESS_ENTER, Glyph::Search);
    search_->text()->SetHint("Find in response");
    search_->SetMinSize(FromDIP(wxSize(220, 30)));
    auto* saveButton = new FlatButton(bodyPage, "Save...", FlatButton::Kind::Ghost);
    saveButton->SetToolTip("Save the response body to a file");

    auto* bodyBar = new wxBoxSizer(wxHORIZONTAL);
    bodyBar->Add(view_, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(12)));
    bodyBar->Add(wrap_, wxSizerFlags().CenterVertical());
    bodyBar->AddStretchSpacer();
    bodyBar->Add(search_, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(4)));
    bodyBar->Add(saveButton, wxSizerFlags().CenterVertical());

    body_ = new CodeEditor(bodyPage, true);
    auto* bodySizer = new wxBoxSizer(wxVERTICAL);
    bodySizer->Add(bodyBar, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
    bodySizer->AddSpacer(FromDIP(6));
    bodySizer->Add(body_, wxSizerFlags(1).Expand());
    bodyPage->SetSizer(bodySizer);
    tabs_->addPage(bodyPage, "Body");

    headers_ = new CodeEditor(tabs_->pageParent(), true, false);
    tabs_->addPage(headers_, "Headers");

    sent_ = new CodeEditor(tabs_->pageParent(), true, false);
    tabs_->addPage(sent_, "Sent");

    pages_->AddPage(tabs_, wxString());

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(pages_, wxSizerFlags(1).Expand());
    SetSizer(root);

    view_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { renderBody(); });
    wrap_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        const bool canWrap = static_cast<std::size_t>(body_->GetLength()) <= kMaxWrappedBytes;
        body_->SetWrapMode(wrap_->GetValue() && canWrap ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
    });
    search_->text()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
        if (!body_->findNext(search_->text()->GetValue())) wxBell();
    });
    saveButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { saveBody(); });

    showIdle();
}

void ResponseView::showMessage(const wxString& title, const wxString& detail, ColourRole titleRole, bool busy) {
    if (busy) {
        spinner_->Show();
        spinner_->Start();
    } else {
        spinner_->Stop();
        spinner_->Hide();
    }
    messageTitle_->setRole(titleRole);
    messageTitle_->SetLabel(title);
    messageDetail_->SetLabel(detail);
    messageDetail_->Wrap(FromDIP(560));
    pages_->ChangeSelection(kMessagePage);
    pages_->GetPage(kMessagePage)->Layout();
}

void ResponseView::showIdle() {
    response_ = HttpResponse{};
    showMessage("No response yet", wxString::Format("Press %s+Enter to send this request.", controlKeyName()),
                ColourRole::Text, false);
}

void ResponseView::showBusy() {
    showMessage("Sending...", wxString(), ColourRole::Text, true);
}

void ResponseView::showError(const wxString& message) {
    showMessage("Can't send yet", message, ColourRole::Danger, false);
}

void ResponseView::showResponse(HttpResponse response) {
    response_ = std::move(response);

    if (!response_.received) {
        if (response_.cancelled) {
            showMessage("Cancelled", wxString(), ColourRole::Text, false);
        } else {
            showMessage("The request failed", toWx(response_.error), ColourRole::Danger, false);
        }
        return;
    }

    spinner_->Stop();
    headers_->setHeaders(response_.headers);
    sent_->setContent(response_.sentLog);
    tabs_->setBadge(1, response_.headers.empty() ? wxString()
                                                 : wxString::Format("%zu", response_.headers.size()));

    wxString details = toWx(formatDuration(response_.totalMs) + "    " + formatBytes(response_.body.size()));
    if (!response_.httpVersion.empty()) details += "    " + toWx(response_.httpVersion);
    if (response_.redirects > 0)
        details += wxString::Format("    %d redirect%s", response_.redirects, response_.redirects == 1 ? "" : "s");
    if (response_.truncated)
        details += wxString::Format("    Only the first %s kept", toWx(formatBytes(response_.body.size())));
    if (!response_.error.empty()) details += "    " + toWx(response_.error);
    details_->SetLabel(details);
    status_->setContent(wxString::Format("%ld %s", response_.status, toWx(response_.statusText)),
                        statusRole(response_.status));
    tabs_->Layout();

    renderBody();
    pages_->ChangeSelection(kContentPage);
}

void ResponseView::renderBody() {
    if (!response_.received) return;

    const std::string* contentTypeHeader = findHeader(response_.headers, "Content-Type");
    const std::string contentType = contentTypeHeader ? *contentTypeHeader : std::string();
    const std::string& raw = response_.body;

    if (looksBinary(raw)) {
        body_->setLanguage(CodeLanguage::Plain);
        body_->setContent("Binary response (" + formatBytes(raw.size()) + "). Use Save to keep it as a file.");
        return;
    }

    std::string text = decodeBody(raw, contentType);
    CodeLanguage language = languageFor(mimeTypeOf(contentType), text);

    const bool pretty = view_->GetSelection() == 0;
    if (!pretty) {
        language = CodeLanguage::Plain;  // just the text as it arrived
    } else if (text.size() <= kMaxPrettyBytes) {
        if (language == CodeLanguage::Html || language == CodeLanguage::Xml) {
            text = prettyMarkup(text, language == CodeLanguage::Html);
        } else if (language == CodeLanguage::Json) {
            if (auto formatted = prettyJson(text)) {
                text = std::move(*formatted);
            } else if (mimeTypeOf(contentType).find("json") == std::string::npos) {
                language = CodeLanguage::Plain;  // only looked like JSON
            }
        }
    }

    body_->SetWrapMode(wrap_->GetValue() && text.size() <= kMaxWrappedBytes ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
    // Set the language first: colouring is done lazily as text scrolls into view.
    body_->setLanguage(text.size() <= kMaxColouredBytes ? language : CodeLanguage::Plain);
    body_->setContent(text);
}

void ResponseView::saveBody() {
    const std::string* contentType = findHeader(response_.headers, "Content-Type");
    const std::string mime = contentType ? mimeTypeOf(*contentType) : std::string();
    wxString fileName = "response";
    if (mime.find("json") != std::string::npos) {
        fileName += ".json";
    } else if (mime.find("html") != std::string::npos) {
        fileName += ".html";
    } else if (mime.find("xml") != std::string::npos) {
        fileName += ".xml";
    } else if (mime.rfind("text/", 0) == 0) {
        fileName += ".txt";
    }
    wxFileDialog dialog(this, "Save response", wxString(), fileName, "All files (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;
    try {
        writeFileAtomic(toPath(dialog.GetPath()), response_.body);
    } catch (const std::exception& e) {
        showNotice(this, "Could not save the response", toWx(e.what()));
    }
}

#include "dialogs.h"

#include "fonts.h"

#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/textctrl.h>
#include <wx/toplevel.h>
#include <wx/utils.h>

#include <algorithm>

ThemedDialog::ThemedDialog(wxWindow* parent, const wxString& title)
    : wxDialog(parent ? wxGetTopLevelParent(parent) : nullptr, wxID_ANY, title) {
    SetFont(uiFont());
    buttons_ = new wxBoxSizer(wxHORIZONTAL);
    buttons_->AddStretchSpacer();

    // wxDialog's own handling of these keys only knows about native buttons.
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        const int code = event.GetKeyCode();
        if (code == WXK_ESCAPE) {
            EndModal(wxID_CANCEL);
        } else if ((code == WXK_RETURN || code == WXK_NUMPAD_ENTER) && defaultButton_ &&
                   !dynamic_cast<FlatControl*>(event.GetEventObject())) {
            if (defaultButton_->IsEnabled()) defaultButton_->click();
        } else {
            event.Skip();
        }
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { EndModal(wxID_CANCEL); });
}

FlatButton* ThemedDialog::addButton(const wxString& label, FlatButton::Kind kind, int id) {
    auto* button = new FlatButton(this, label, kind, Glyph::None, id);
    button->SetMinSize(wxSize(std::max(FromDIP(84), button->GetBestSize().x), FromDIP(34)));
    buttons_->Add(button, wxSizerFlags().Border(wxLEFT, FromDIP(8)));
    button->Bind(wxEVT_BUTTON, [this, id](wxCommandEvent&) {
        if (canFinish(id)) EndModal(id);
    });
    return button;
}

void ThemedDialog::finish(wxSizer* content) {
    const int margin = FromDIP(20);
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->AddSpacer(margin);
    root->Add(content, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, margin));
    root->AddSpacer(margin);
    root->Add(buttons_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, margin));
    root->AddSpacer(margin);
    SetSizerAndFit(root);
    applyThemeTo(this);
    // The window may not be up yet, when a problem turns up during startup.
    if (GetParent() && GetParent()->IsShown()) {
        CentreOnParent();
    } else {
        CentreOnScreen();
    }
    // Starting on a button, with the ring showing, makes clear what Enter does.
    if (defaultButton_) defaultButton_->SetFocusFromKbd();
}

namespace {

class MessageDialog : public ThemedDialog {
public:
    using ThemedDialog::ThemedDialog;

    FlatButton* button(const wxString& label, FlatButton::Kind kind, int id, bool isDefault = false) {
        FlatButton* added = addButton(label, kind, id);
        if (isDefault) setDefaultButton(added);
        return added;
    }

    int ask(const wxString& message) {
        auto* label = new RoleLabel(this, message, ColourRole::Text);
        label->Wrap(FromDIP(440));
        auto* content = new wxBoxSizer(wxVERTICAL);
        content->SetMinSize(wxSize(FromDIP(360), 0));
        content->Add(label);
        finish(content);
        return ShowModal();
    }
};

class TextDialog : public ThemedDialog {
public:
    TextDialog(wxWindow* parent, const wxString& title, const wxString& prompt, const wxString& initial)
        : ThemedDialog(parent, title) {
        field_ = new Field(this);
        field_->SetMinSize(wxSize(FromDIP(360), FromDIP(34)));
        field_->text()->SetValue(initial);

        addButton("Cancel", FlatButton::Kind::Subtle, wxID_CANCEL);
        auto* ok = addButton("OK", FlatButton::Kind::Primary, wxID_OK);
        setDefaultButton(ok);

        auto* content = new wxBoxSizer(wxVERTICAL);
        content->Add(new MutedLabel(this, prompt));
        content->AddSpacer(FromDIP(6));
        content->Add(field_, wxSizerFlags().Expand());
        finish(content);

        const auto update = [this, ok] { ok->Enable(!value().empty()); };
        update();
        field_->text()->Bind(wxEVT_TEXT, [update](wxCommandEvent&) { update(); });
        field_->text()->SetFocus();
        field_->text()->SelectAll();
    }

    wxString value() const { return field_->text()->GetValue().Strip(wxString::both); }

private:
    Field* field_;
};

class AboutDialog : public ThemedDialog {
public:
    AboutDialog(wxWindow* parent, const wxBitmap& icon) : ThemedDialog(parent, "About Fetcher") {
        auto* name = new RoleLabel(this, "Fetcher", ColourRole::Text);
        name->SetFont(name->GetFont().Bold().Scaled(1.5f));

        auto* text = new wxBoxSizer(wxVERTICAL);
        text->Add(name);
        text->Add(new MutedLabel(this, wxString("Version ") + FETCHER_VERSION));
        text->AddSpacer(FromDIP(10));
        text->Add(new RoleLabel(this, "Test API calls, modify them and save them.", ColourRole::Text));

        auto* content = new wxBoxSizer(wxHORIZONTAL);
        content->SetMinSize(wxSize(FromDIP(360), 0));
        if (icon.IsOk()) {
            content->Add(new wxStaticBitmap(this, wxID_ANY, icon));
            content->AddSpacer(FromDIP(16));
        }
        content->Add(text, wxSizerFlags(1));

        addButton("View on GitHub", FlatButton::Kind::Subtle, wxID_HELP);
        setDefaultButton(addButton("Close", FlatButton::Kind::Primary, wxID_OK));
        finish(content);
    }

protected:
    bool canFinish(int id) override {
        if (id != wxID_HELP) return true;
        wxLaunchDefaultBrowser("https://github.com/jbktn/Fetcher");
        return false;
    }
};

}  // namespace

void showNotice(wxWindow* parent, const wxString& title, const wxString& message) {
    MessageDialog dialog(parent, title);
    dialog.button("OK", FlatButton::Kind::Primary, wxID_OK, true);
    dialog.ask(message);
}

bool confirmDelete(wxWindow* parent, const wxString& title, const wxString& message) {
    MessageDialog dialog(parent, title);
    dialog.button("Keep", FlatButton::Kind::Subtle, wxID_CANCEL, true);
    dialog.button("Delete", FlatButton::Kind::Danger, wxID_DELETE);
    return dialog.ask(message) == wxID_DELETE;
}

SaveAnswer askToSave(wxWindow* parent, const wxString& message) {
    MessageDialog dialog(parent, "Unsaved Changes");
    dialog.button("Cancel", FlatButton::Kind::Subtle, wxID_CANCEL);
    dialog.button("Don't Save", FlatButton::Kind::Subtle, wxID_NO);
    dialog.button("Save", FlatButton::Kind::Primary, wxID_SAVE, true);
    switch (dialog.ask(message)) {
        case wxID_SAVE: return SaveAnswer::Save;
        case wxID_NO: return SaveAnswer::DontSave;
        default: return SaveAnswer::Cancel;
    }
}

wxString askForText(wxWindow* parent, const wxString& title, const wxString& prompt, const wxString& initial) {
    TextDialog dialog(parent, title, prompt, initial);
    return dialog.ShowModal() == wxID_OK ? dialog.value() : wxString();
}

void showAbout(wxWindow* parent, const wxBitmap& icon) {
    AboutDialog dialog(parent, icon);
    dialog.ShowModal();
}

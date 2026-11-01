// Dialogs drawn with the app's own controls and colours. File pickers stay native.
#pragma once

#include "widgets.h"

#include <wx/bitmap.h>
#include <wx/dialog.h>

class wxSizer;

// A modal dialog with content above a right-aligned row of buttons. Esc cancels,
// and Enter presses the default button unless the focus is on another button.
class ThemedDialog : public wxDialog {
public:
    ThemedDialog(wxWindow* parent, const wxString& title);

protected:
    // A click ends the dialog with `id`, if canFinish(id) agrees.
    FlatButton* addButton(const wxString& label, FlatButton::Kind kind, int id);
    void setDefaultButton(FlatButton* button) { defaultButton_ = button; }
    // Lays everything out. Call once the content and buttons all exist.
    void finish(wxSizer* content);
    virtual bool canFinish(int /*id*/) { return true; }

private:
    wxBoxSizer* buttons_;
    FlatButton* defaultButton_ = nullptr;
};

void showNotice(wxWindow* parent, const wxString& title, const wxString& message);

// True if the user chose Delete. Keep is the default.
bool confirmDelete(wxWindow* parent, const wxString& title, const wxString& message);

enum class SaveAnswer { Save, DontSave, Cancel };
SaveAnswer askToSave(wxWindow* parent, const wxString& message);

// The trimmed text, or an empty string if the user cancelled.
wxString askForText(wxWindow* parent, const wxString& title, const wxString& prompt, const wxString& initial);

void showAbout(wxWindow* parent, const wxBitmap& icon);

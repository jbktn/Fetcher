#include "key_value_editor.h"

#include "theme.h"
#include "widgets.h"

#include <wx/dcclient.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

#include <algorithm>

wxDEFINE_EVENT(EVT_ROWS_CHANGED, wxCommandEvent);

class KeyValueEditor::Row : public wxPanel {
public:
    Row(wxWindow* parent, const wxString& keyHint, const wxString& valueHint)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE) {
        SetMinSize(wxSize(-1, FromDIP(36)));

        enabled = new FlatCheck(this);
        enabled->setKeyboardFocusable(false);
        enabled->SetToolTip("Include this row");
        key = new wxTextCtrl(this, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        key->SetHint(keyHint);
        value = new wxTextCtrl(this, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        value->SetHint(valueHint);
        remove = new FlatButton(this, wxString(), FlatButton::Kind::Ghost, Glyph::Close);
        remove->setKeyboardFocusable(false);
        remove->SetToolTip("Remove this row");

        const int gap = FromDIP(12);
        auto* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(enabled, wxSizerFlags().CenterVertical().Border(wxLEFT, gap).ReserveSpaceEvenIfHidden());
        sizer->Add(key, wxSizerFlags(2).CenterVertical().Border(wxLEFT, gap));
        sizer->AddSpacer(2 * gap);
        sizer->Add(value, wxSizerFlags(3).CenterVertical());
        sizer->Add(remove, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(4)).ReserveSpaceEvenIfHidden());
        SetSizer(sizer);

        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxPaintDC dc(this);
            const Palette& colours = palette();
            const wxSize size = GetClientSize();
            const int line = std::max(1, FromDIP(1));
            const bool focused = key->HasFocus() || value->HasFocus();
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(focused ? colours.accent : colours.border));
            dc.DrawRectangle(0, size.y - line, size.x, line);
            dc.SetBrush(wxBrush(colours.border));
            dc.DrawRectangle(value->GetPosition().x - FromDIP(12), FromDIP(8), line, size.y - 2 * FromDIP(8));
        });
        const auto focusChanged = [this](wxFocusEvent& event) {
            Refresh();
            event.Skip();
        };
        key->Bind(wxEVT_SET_FOCUS, focusChanged);
        key->Bind(wxEVT_KILL_FOCUS, focusChanged);
        value->Bind(wxEVT_SET_FOCUS, focusChanged);
        value->Bind(wxEVT_KILL_FOCUS, focusChanged);
    }

    FlatCheck* enabled;
    wxTextCtrl* key;
    wxTextCtrl* value;
    FlatButton* remove;
};

KeyValueEditor::KeyValueEditor(wxWindow* parent, const wxString& keyHint, const wxString& valueHint)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE),
      keyHint_(keyHint),
      valueHint_(valueHint) {
    SetScrollRate(0, FromDIP(12));
    list_ = new wxBoxSizer(wxVERTICAL);
    SetSizer(list_);
    addRow();
    updateRowControls();
}

void KeyValueEditor::setRows(const std::vector<KeyValue>& rows) {
    const std::size_t wanted = rows.size() + 1;
    if (rows_.size() != wanted) {
        while (rows_.size() < wanted) addRow();
        while (rows_.size() > wanted) removeLastRow();
        relayout();
    }
    const KeyValue blank;
    for (std::size_t i = 0; i < wanted; ++i) {
        const KeyValue& source = i < rows.size() ? rows[i] : blank;
        // Rewriting identical text would move the caret, so only touch what changed.
        if (rows_[i]->key->GetValue() != toWx(source.key)) rows_[i]->key->ChangeValue(toWx(source.key));
        if (rows_[i]->value->GetValue() != toWx(source.value)) rows_[i]->value->ChangeValue(toWx(source.value));
        rows_[i]->enabled->SetValue(source.enabled);
    }
    updateRowControls();
}

std::vector<KeyValue> KeyValueEditor::rows() const {
    std::vector<KeyValue> out;
    for (const Row* row : rows_) {
        KeyValue value = rowValue(*row);
        if (!value.key.empty() || !value.value.empty()) out.push_back(std::move(value));
    }
    return out;
}

void KeyValueEditor::setKeySuggestions(const wxArrayString& suggestions) {
    suggestions_ = suggestions;
    for (Row* row : rows_) row->key->AutoComplete(suggestions_);
}

void KeyValueEditor::addRow() {
    auto* row = new Row(this, keyHint_, valueHint_);
    if (!suggestions_.empty()) row->key->AutoComplete(suggestions_);
    list_->Add(row, wxSizerFlags().Expand());

    row->enabled->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { edited(); });
    row->key->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { edited(); });
    row->value->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { edited(); });
    row->remove->Bind(wxEVT_BUTTON, [this, row](wxCommandEvent&) {
        const auto it = std::find(rows_.begin(), rows_.end(), row);
        if (it == rows_.end()) return;
        const auto index = static_cast<std::size_t>(it - rows_.begin());
        // Deferred because this may destroy the button being clicked.
        CallAfter([this, index] { removeRow(index); });
    });

    rows_.push_back(row);
    applyThemeTo(row);
}

void KeyValueEditor::removeLastRow() {
    Row* row = rows_.back();
    rows_.pop_back();
    list_->Detach(row);
    row->Destroy();
}

void KeyValueEditor::removeRow(std::size_t index) {
    if (index >= rows_.size()) return;
    std::vector<KeyValue> values;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (i != index) values.push_back(rowValue(*rows_[i]));
    while (!values.empty() && values.back().key.empty() && values.back().value.empty()) values.pop_back();
    setRows(values);
    notifyChanged();
}

void KeyValueEditor::edited() {
    ensureBlankLastRow();
    updateRowControls();
    notifyChanged();
}

void KeyValueEditor::notifyChanged() {
    wxCommandEvent event(EVT_ROWS_CHANGED, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}

void KeyValueEditor::ensureBlankLastRow() {
    const Row* last = rows_.back();
    if (last->key->IsEmpty() && last->value->IsEmpty()) return;
    addRow();
    relayout();
}

void KeyValueEditor::updateRowControls() {
    // The blank row is only an invitation to type, so it has nothing to switch off or remove.
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool filled = i + 1 < rows_.size();
        rows_[i]->enabled->Show(filled);
        rows_[i]->remove->Show(filled);
    }
}

void KeyValueEditor::relayout() {
    FitInside();
    Layout();
}

KeyValue KeyValueEditor::rowValue(const Row& row) const {
    KeyValue value;
    value.key = toUtf8(row.key->GetValue());
    value.value = toUtf8(row.value->GetValue());
    value.enabled = row.enabled->GetValue();
    return value;
}

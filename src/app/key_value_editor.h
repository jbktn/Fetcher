// An editable list of key/value rows, separated by hairlines. Keys and values
// are ordinary native text boxes, so typing, Tab and the clipboard behave the way
// the platform expects. A blank row at the end is always ready for new input.
#pragma once

#include "core/model.h"
#include "wx_util.h"

#include <wx/arrstr.h>
#include <wx/event.h>
#include <wx/scrolwin.h>

#include <vector>

class wxBoxSizer;

// Sent when the user changes anything in the list, not by setRows().
wxDECLARE_EVENT(EVT_ROWS_CHANGED, wxCommandEvent);

class KeyValueEditor : public wxScrolledWindow {
public:
    KeyValueEditor(wxWindow* parent, const wxString& keyHint, const wxString& valueHint);

    void setRows(const std::vector<KeyValue>& rows);
    // Rows the user has filled in; blank rows are left out.
    std::vector<KeyValue> rows() const;

    // Offered as completions in the key column.
    void setKeySuggestions(const wxArrayString& suggestions);

private:
    class Row;

    void addRow();
    void removeLastRow();
    void removeRow(std::size_t index);
    void edited();
    void notifyChanged();
    void ensureBlankLastRow();
    void updateRowControls();
    void relayout();
    KeyValue rowValue(const Row& row) const;

    wxBoxSizer* list_;
    const wxString keyHint_;
    const wxString valueHint_;
    std::vector<Row*> rows_;
    wxArrayString suggestions_;
};

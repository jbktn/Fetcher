// Monospaced text editor with syntax colouring, used for request and response
// bodies.
#pragma once

#include "core/http_client.h"
#include "theme.h"

#include <wx/stc/stc.h>

#include <string>
#include <string_view>

enum class CodeLanguage { Plain, Json, Xml, Html, Headers };

class CodeEditor : public wxStyledTextCtrl, public ThemeAware {
public:
    CodeEditor(wxWindow* parent, bool readOnly, bool lineNumbers = true);

    void setLanguage(CodeLanguage language);
    // Takes UTF-8 text and clears the undo history.
    void setContent(std::string_view text);
    std::string content() const;

    // Shows "Name: value" lines with the names coloured.
    void setHeaders(const fetcher::HeaderList& headers);

    // Selects the next match after the current selection, wrapping around.
    bool findNext(const wxString& query);

    void applyTheme() override;

private:
    void updateLineNumberMargin();
    void indentNewLine(int character);

    const bool readOnly_;
    const bool lineNumbers_;
    CodeLanguage language_ = CodeLanguage::Plain;
    int marginDigits_ = 0;
};

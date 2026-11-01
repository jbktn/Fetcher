#include "code_editor.h"

#include "fonts.h"

#include <wx/settings.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

constexpr int kHeaderNameStyle = 1;

}  // namespace

CodeEditor::CodeEditor(wxWindow* parent, bool readOnly, bool lineNumbers)
    : wxStyledTextCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
      readOnly_(readOnly),
      lineNumbers_(lineNumbers) {
    SetEOLMode(wxSTC_EOL_LF);
    SetTabWidth(2);
    SetUseTabs(false);
    SetIndent(2);
    SetTabIndents(true);
    SetBackSpaceUnIndents(true);
    SetWrapMode(wxSTC_WRAP_WORD);
    SetMarginType(0, wxSTC_MARGIN_NUMBER);
    SetMarginWidth(1, 0);
    SetMarginWidth(2, 0);
    SetMargins(FromDIP(10), FromDIP(10));
    SetExtraAscent(1);
    SetExtraDescent(1);
    SetScrollWidth(1);
    SetScrollWidthTracking(true);
    SetReadOnly(readOnly);
    applyTheme();
    updateLineNumberMargin();

    Bind(wxEVT_STC_CHANGE, [this](wxStyledTextEvent& event) {
        updateLineNumberMargin();
        event.Skip();
    });
    if (!readOnly) {
        Bind(wxEVT_STC_CHARADDED, [this](wxStyledTextEvent& event) {
            indentNewLine(event.GetKey());
            event.Skip();
        });
    }
}

void CodeEditor::setLanguage(CodeLanguage language) {
    language_ = language;
    switch (language) {
        case CodeLanguage::Json: SetLexer(wxSTC_LEX_JSON); break;
        case CodeLanguage::Xml: SetLexer(wxSTC_LEX_XML); break;
        case CodeLanguage::Html: SetLexer(wxSTC_LEX_HTML); break;
        case CodeLanguage::Plain: SetLexer(wxSTC_LEX_NULL); break;
        // Styled by setHeaders(); a lexer would overwrite that.
        case CodeLanguage::Headers: SetLexer(wxSTC_LEX_CONTAINER); break;
    }
    applyTheme();
    if (language != CodeLanguage::Headers) Colourise(0, -1);
}

void CodeEditor::setContent(std::string_view text) {
    SetReadOnly(false);
    ClearAll();
    AddTextRaw(text.data(), static_cast<int>(text.size()));
    GotoPos(0);
    EmptyUndoBuffer();
    SetSavePoint();
    SetReadOnly(readOnly_);
    updateLineNumberMargin();
}

std::string CodeEditor::content() const {
    const wxCharBuffer buffer = const_cast<CodeEditor*>(this)->GetTextRaw();
    return std::string(buffer.data(), buffer.length());
}

void CodeEditor::setHeaders(const fetcher::HeaderList& headers) {
    setLanguage(CodeLanguage::Headers);
    std::string text;
    std::vector<std::pair<std::size_t, std::size_t>> names;
    for (const auto& [name, value] : headers) {
        names.emplace_back(text.size(), name.size() + 1);
        text += name + ": " + value + "\n";
    }
    setContent(text);
    for (const auto& [start, length] : names) {
        StartStyling(static_cast<int>(start));
        SetStyling(static_cast<int>(length), kHeaderNameStyle);
    }
}

bool CodeEditor::findNext(const wxString& query) {
    if (query.empty()) return false;
    const int start = GetSelectionEnd();
    SetSearchFlags(0);
    SetTargetStart(start);
    SetTargetEnd(GetLength());
    int found = SearchInTarget(query);
    if (found < 0) {
        SetTargetStart(0);
        SetTargetEnd(start);
        found = SearchInTarget(query);
    }
    if (found < 0) return false;
    SetSelection(GetTargetStart(), GetTargetEnd());
    EnsureCaretVisible();
    return true;
}

void CodeEditor::applyTheme() {
    const Palette& colours = palette();

    StyleSetFont(wxSTC_STYLE_DEFAULT, codeFont());
    StyleSetForeground(wxSTC_STYLE_DEFAULT, colours.text);
    StyleSetBackground(wxSTC_STYLE_DEFAULT, colours.background);
    StyleClearAll();

    StyleSetForeground(wxSTC_STYLE_LINENUMBER, mixColours(colours.background, colours.muted, 0.7));
    StyleSetBackground(wxSTC_STYLE_LINENUMBER, colours.background);
    SetCaretForeground(colours.text);
    SetSelBackground(true, mixColours(colours.background, colours.accent, 0.35));
    SetSelForeground(false, colours.text);

    if (language_ == CodeLanguage::Json) {
        StyleSetForeground(wxSTC_JSON_PROPERTYNAME, colours.synProperty);
        StyleSetForeground(wxSTC_JSON_STRING, colours.synString);
        StyleSetForeground(wxSTC_JSON_STRINGEOL, colours.synString);
        StyleSetForeground(wxSTC_JSON_URI, colours.synString);
        StyleSetForeground(wxSTC_JSON_ESCAPESEQUENCE, colours.synKeyword);
        StyleSetForeground(wxSTC_JSON_NUMBER, colours.synNumber);
        StyleSetForeground(wxSTC_JSON_KEYWORD, colours.synKeyword);
        StyleSetForeground(wxSTC_JSON_OPERATOR, colours.synPunctuation);
        StyleSetForeground(wxSTC_JSON_ERROR, colours.synNumber);
    } else if (language_ == CodeLanguage::Xml || language_ == CodeLanguage::Html) {
        StyleSetForeground(wxSTC_H_TAG, colours.synProperty);
        StyleSetForeground(wxSTC_H_TAGUNKNOWN, colours.synProperty);
        StyleSetForeground(wxSTC_H_ATTRIBUTE, colours.synNumber);
        StyleSetForeground(wxSTC_H_ATTRIBUTEUNKNOWN, colours.synNumber);
        StyleSetForeground(wxSTC_H_DOUBLESTRING, colours.synString);
        StyleSetForeground(wxSTC_H_SINGLESTRING, colours.synString);
        StyleSetForeground(wxSTC_H_VALUE, colours.synString);
        StyleSetForeground(wxSTC_H_NUMBER, colours.synNumber);
        StyleSetForeground(wxSTC_H_ENTITY, colours.synKeyword);
        StyleSetForeground(wxSTC_H_COMMENT, colours.synComment);
        StyleSetForeground(wxSTC_H_CDATA, colours.synString);
        StyleSetForeground(wxSTC_H_OTHER, colours.synNumber);
        StyleSetForeground(wxSTC_H_XMLSTART, colours.synKeyword);
        StyleSetForeground(wxSTC_H_XMLEND, colours.synKeyword);
    } else if (language_ == CodeLanguage::Headers) {
        StyleSetForeground(kHeaderNameStyle, colours.synProperty);
    }
    marginDigits_ = 0;  // the font may have changed, so recompute the width
    updateLineNumberMargin();
}

void CodeEditor::updateLineNumberMargin() {
    if (!lineNumbers_) {
        SetMarginWidth(0, 0);
        return;
    }
    int digits = 2;
    for (int lines = GetLineCount(); lines >= 100; lines /= 10) ++digits;
    if (digits == marginDigits_) return;
    marginDigits_ = digits;
    SetMarginWidth(0, TextWidth(wxSTC_STYLE_LINENUMBER, wxString('9', digits + 1)));
}

void CodeEditor::indentNewLine(int character) {
    if (character != '\n') return;
    const int line = GetCurrentLine();
    if (line == 0) return;
    const int indent = GetLineIndentation(line - 1);
    if (indent == 0) return;
    SetLineIndentation(line, indent);
    GotoPos(GetLineIndentPosition(line));
}

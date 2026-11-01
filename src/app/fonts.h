// The typefaces that ship with the app, so text looks the same on every platform.
#pragma once

#include <wx/font.h>

// Call once at startup, before creating any window or graphics context. If the
// font files are missing, the app carries on with the platform's fonts.
void loadBundledFonts();

// Roboto at the platform's usual text size. Windows inherit it from a parent that
// has it set, so top-level windows are the ones to give it to.
wxFont uiFont();
// Roboto Mono, for the code editors.
wxFont codeFont();

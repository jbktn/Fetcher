#include "fonts.h"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>

namespace {

bool haveUiFont = false;
bool haveCodeFont = false;

wxString fontsDirectory() {
#ifdef __WXOSX__
    // The bundle's Info.plist points the system at this folder.
    return wxStandardPaths::Get().GetResourcesDir() + "/Fonts";
#else
    const wxString executableDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    const wxString besideExecutable = executableDirectory + wxFILE_SEP_PATH + "fonts";
    if (wxDirExists(besideExecutable)) return besideExecutable;
    // Installed on Linux: bin/fetcher and share/fetcher/fonts.
    return executableDirectory + "/../share/fetcher/fonts";
#endif
}

bool addFont(const wxString& directory, const wxString& fileName) {
    wxLogNull noErrorBoxes;  // a missing font isn't worth interrupting anyone over
    return wxFont::AddPrivateFont(directory + wxFILE_SEP_PATH + fileName);
}

}  // namespace

void loadBundledFonts() {
    const wxString directory = fontsDirectory();
    // Not short-circuited: bold is a separate file, and regular must still load.
    const bool regular = addFont(directory, "Roboto-Regular.ttf");
    const bool bold = addFont(directory, "Roboto-Bold.ttf");
    haveUiFont = regular && bold;
    haveCodeFont = addFont(directory, "RobotoMono-Regular.ttf");
}

wxFont uiFont() {
    wxFont font = *wxNORMAL_FONT;
    if (haveUiFont) font.SetFaceName("Roboto");
    return font;
}

wxFont codeFont() {
    wxFontInfo info(wxNORMAL_FONT->GetPointSize());
    info.Family(wxFONTFAMILY_TELETYPE);
    if (haveCodeFont) {
        info.FaceName("Roboto Mono");
    } else {
#if defined(__WXMSW__)
        info.FaceName("Consolas");
#elif defined(__WXOSX__)
        info.FaceName("Menlo");
#endif
    }
    return wxFont(info);
}

#include "fonts.h"
#include "main_frame.h"
#include "theme.h"
#include "wx_util.h"

#include <wx/app.h>
#include <wx/imagpng.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

class FetcherApp : public wxApp {
public:
    bool OnInit() override {
#ifndef FETCHER_SHOW_ASSERTS
        wxDisableAsserts();
#endif
        SetAppName("Fetcher");
        SetVendorName("Fetcher");
#ifdef __WXGTK__
        wxStandardPaths::Get().SetFileLayout(wxStandardPaths::FileLayout_XDG);
#endif
        if (!wxApp::OnInit()) return false;

        loadBundledFonts();
        initTheme();
        wxImage::AddHandler(new wxPNGHandler);

        // FETCHER_DATA_DIR keeps data somewhere else, e.g. for a portable setup.
        wxString dataDirectory;
        if (!wxGetEnv("FETCHER_DATA_DIR", &dataDirectory) || dataDirectory.empty())
            dataDirectory = wxStandardPaths::Get().GetUserDataDir();

        auto* frame = new MainFrame(toPath(dataDirectory));
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(FetcherApp);

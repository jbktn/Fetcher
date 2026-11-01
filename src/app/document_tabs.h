// The strip of open requests along the top of the window, and their pages.
#pragma once

#include "theme.h"

#include <wx/panel.h>

#include <functional>
#include <vector>

class wxBoxSizer;
class PageBook;
class TabStrip;

class DocumentTabs : public wxPanel {
public:
    explicit DocumentTabs(wxWindow* parent);

    // Called when the user clicks a tab's close button or the new-tab button.
    std::function<void(std::size_t index)> onCloseRequested;
    std::function<void()> onNewRequested;

    // New pages must be children of this window.
    wxWindow* pageParent();

    void addPage(wxWindow* page, const wxString& title, bool dirty);
    void removePage(wxWindow* page);  // also destroys it

    std::size_t pageCount() const { return order_.size(); }
    wxWindow* page(std::size_t index) const { return order_[index]; }
    int indexOf(wxWindow* page) const;
    int selection() const { return selected_; }
    void select(std::size_t index);
    void selectRelative(int offset);
    void setTitle(wxWindow* page, const wxString& title, bool dirty);

    // Adds a control at the far right of the strip.
    void addTrailing(wxWindow* window);

private:
    friend class TabStrip;
    struct Tab {
        wxString title;
        bool dirty;
    };

    void moveTab(std::size_t from, std::size_t to);

    TabStrip* strip_;
    PageBook* book_;
    wxBoxSizer* header_;
    std::vector<wxWindow*> order_;
    std::vector<Tab> tabs_;
    int selected_ = wxNOT_FOUND;
};

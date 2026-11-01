#include "document_tabs.h"

#include "widgets.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/sizer.h>

#include <algorithm>
#include <memory>

class TabStrip : public wxWindow, public ThemeAware {
public:
    explicit TabStrip(DocumentTabs& owner) : owner_(owner) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Create(&owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
        SetMinSize(wxSize(-1, FromDIP(40)));

        Bind(wxEVT_PAINT, &TabStrip::onPaint, this);
        Bind(wxEVT_MOTION, &TabStrip::onMotion, this);
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
            setHover(Hit{});
            event.Skip();
        });
        Bind(wxEVT_LEFT_DOWN, &TabStrip::onLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &TabStrip::onLeftDown, this);
        Bind(wxEVT_LEFT_UP, &TabStrip::onLeftUp, this);
        Bind(wxEVT_MIDDLE_UP, [this](wxMouseEvent& event) {
            const Hit hit = hitTest(event.GetPosition());
            if (hit.tab != wxNOT_FOUND) requestClose(static_cast<std::size_t>(hit.tab));
        });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { dragging_ = wxNOT_FOUND; });
    }

    void applyTheme() override { Refresh(); }

private:
    struct Hit {
        int tab = wxNOT_FOUND;
        bool close = false;
        bool plus = false;
        bool operator!=(const Hit& o) const { return tab != o.tab || close != o.close || plus != o.plus; }
    };

    int closeSize() const { return FromDIP(18); }

    std::vector<wxRect> tabRects() const {
        const int count = static_cast<int>(owner_.tabs_.size());
        std::vector<wxRect> rects;
        if (count == 0) return rects;

        const int minimum = FromDIP(72);
        const int maximum = FromDIP(220);
        const int padding = FromDIP(12);
        const int available = GetClientSize().x - FromDIP(8) - plusSize();

        std::vector<int> widths;
        int total = 0;
        for (const auto& tab : owner_.tabs_) {
            const int natural = GetTextExtent(tab.title).x + 2 * padding + closeSize();
            widths.push_back(std::clamp(natural, FromDIP(96), maximum));
            total += widths.back();
        }
        // Like a browser, tabs share the width evenly once they no longer fit.
        if (total > available) std::fill(widths.begin(), widths.end(), std::max(minimum, available / count));

        const int height = GetClientSize().y;
        int x = FromDIP(4);
        for (int width : widths) {
            rects.emplace_back(x, 0, width, height);
            x += width;
        }
        return rects;
    }

    int plusSize() const { return FromDIP(32); }

    wxRect plusRect(const std::vector<wxRect>& tabs) const {
        const int x = tabs.empty() ? FromDIP(4) : tabs.back().GetRight() + 1 + FromDIP(2);
        const int size = FromDIP(28);
        return wxRect(x, (GetClientSize().y - size) / 2, size, size);
    }

    wxRect closeRect(const wxRect& tab) const {
        const int size = closeSize();
        return wxRect(tab.GetRight() - size - FromDIP(6), (tab.height - size) / 2, size, size);
    }

    Hit hitTest(const wxPoint& point) const {
        Hit hit;
        const auto rects = tabRects();
        if (plusRect(rects).Contains(point)) {
            hit.plus = true;
            return hit;
        }
        for (std::size_t i = 0; i < rects.size(); ++i) {
            if (!rects[i].Contains(point)) continue;
            hit.tab = static_cast<int>(i);
            hit.close = closeRect(rects[i]).Contains(point);
            break;
        }
        return hit;
    }

    void setHover(const Hit& hit) {
        if (hover_ != hit) {
            hover_ = hit;
            Refresh();
        }
    }

    void requestClose(std::size_t index) {
        // Deferred: closing can show a dialog, and destroys pages.
        CallAfter([this, index] {
            if (owner_.onCloseRequested) owner_.onCloseRequested(index);
        });
    }

    void onMotion(wxMouseEvent& event) {
        const Hit hit = hitTest(event.GetPosition());
        setHover(hit);
        if (dragging_ != wxNOT_FOUND && event.LeftIsDown() && hit.tab != wxNOT_FOUND && hit.tab != dragging_) {
            owner_.moveTab(static_cast<std::size_t>(dragging_), static_cast<std::size_t>(hit.tab));
            dragging_ = hit.tab;
        }
        event.Skip();
    }

    void onLeftDown(wxMouseEvent& event) {
        const Hit hit = hitTest(event.GetPosition());
        if (hit.plus) {
            CallAfter([this] {
                if (owner_.onNewRequested) owner_.onNewRequested();
            });
        } else if (hit.tab != wxNOT_FOUND && hit.close) {
            requestClose(static_cast<std::size_t>(hit.tab));
        } else if (hit.tab != wxNOT_FOUND) {
            owner_.select(static_cast<std::size_t>(hit.tab));
            dragging_ = hit.tab;
            if (!HasCapture()) CaptureMouse();
        }
    }

    void onLeftUp(wxMouseEvent&) {
        dragging_ = wxNOT_FOUND;
        if (HasCapture()) ReleaseMouse();
    }

    void onPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const Palette& colours = palette();
        dc.SetBackground(wxBrush(colours.background));
        dc.Clear();

        const auto rects = tabRects();
        const int selected = owner_.selected_;
        {
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
            if (gc) {
                for (std::size_t i = 0; i < rects.size(); ++i) {
                    const bool isSelected = static_cast<int>(i) == selected;
                    const bool isHovered = static_cast<int>(i) == hover_.tab;
                    const wxRect close = closeRect(rects[i]);
                    if (isSelected) {
                        const int thickness = FromDIP(2);
                        gc->SetPen(*wxTRANSPARENT_PEN);
                        gc->SetBrush(wxBrush(colours.accent));
                        gc->DrawRoundedRectangle(rects[i].x + FromDIP(8), rects[i].height - thickness,
                                                 rects[i].width - FromDIP(16), thickness, thickness / 2.0);
                    }
                    if (isHovered && hover_.close) {
                        gc->SetPen(*wxTRANSPARENT_PEN);
                        gc->SetBrush(wxBrush(colours.hover));
                        gc->DrawRoundedRectangle(close.x, close.y, close.width, close.height, FromDIP(4));
                    }
                    if (owner_.tabs_[i].dirty && !isHovered) {
                        // A dot marks unsaved changes until the pointer asks for the X.
                        gc->SetPen(*wxTRANSPARENT_PEN);
                        gc->SetBrush(wxBrush(colours.accent));
                        const double dot = FromDIP(7);
                        gc->DrawEllipse(close.x + (close.width - dot) / 2, close.y + (close.height - dot) / 2, dot, dot);
                    } else if (isSelected || isHovered) {
                        const double size = FromDIP(14);
                        drawGlyph(*gc, Glyph::Close, close.x + (close.width - size) / 2,
                                  close.y + (close.height - size) / 2, size, colours.muted);
                    }
                }
                const wxRect plus = plusRect(rects);
                if (hover_.plus) {
                    gc->SetPen(*wxTRANSPARENT_PEN);
                    gc->SetBrush(wxBrush(colours.hover));
                    gc->DrawRoundedRectangle(plus.x, plus.y, plus.width, plus.height, FromDIP(6));
                }
                const double size = FromDIP(16);
                drawGlyph(*gc, Glyph::Plus, plus.x + (plus.width - size) / 2, plus.y + (plus.height - size) / 2, size,
                          hover_.plus ? colours.text : colours.muted);
            }
        }

        dc.SetFont(GetFont());
        for (std::size_t i = 0; i < rects.size(); ++i) {
            const bool active = static_cast<int>(i) == selected || static_cast<int>(i) == hover_.tab;
            const int padding = FromDIP(12);
            const int room = rects[i].width - padding - closeSize() - FromDIP(8);
            const wxString title = wxControl::Ellipsize(owner_.tabs_[i].title, dc, wxELLIPSIZE_END, room);
            const wxSize text = dc.GetTextExtent(title);
            dc.SetTextForeground(active ? colours.text : colours.muted);
            dc.DrawText(title, rects[i].x + padding, (rects[i].height - text.y) / 2);
        }
    }

    DocumentTabs& owner_;
    Hit hover_;
    int dragging_ = wxNOT_FOUND;
};

wxWindow* DocumentTabs::pageParent() {
    return book_;
}

DocumentTabs::DocumentTabs(wxWindow* parent) : wxPanel(parent) {
    strip_ = new TabStrip(*this);
    book_ = new PageBook(this);

    header_ = new wxBoxSizer(wxHORIZONTAL);
    header_->Add(strip_, wxSizerFlags(1).Expand());

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(header_, wxSizerFlags().Expand());
    root->Add(new Separator(this), wxSizerFlags().Expand());
    root->Add(book_, wxSizerFlags(1).Expand());
    SetSizer(root);
}

void DocumentTabs::addPage(wxWindow* page, const wxString& title, bool dirty) {
    order_.push_back(page);
    tabs_.push_back({title, dirty});
    book_->AddPage(page, title);
    select(order_.size() - 1);
}

void DocumentTabs::removePage(wxWindow* page) {
    const int index = indexOf(page);
    if (index == wxNOT_FOUND) return;
    const auto position = static_cast<std::size_t>(index);

    order_.erase(order_.begin() + index);
    tabs_.erase(tabs_.begin() + index);
    const int bookIndex = book_->FindPage(page);
    if (bookIndex != wxNOT_FOUND) book_->DeletePage(static_cast<std::size_t>(bookIndex));

    if (order_.empty()) {
        selected_ = wxNOT_FOUND;
    } else if (selected_ == index) {
        selected_ = wxNOT_FOUND;
        select(std::min(position, order_.size() - 1));
    } else if (selected_ > index) {
        --selected_;
    }
    strip_->Refresh();
}

int DocumentTabs::indexOf(wxWindow* page) const {
    const auto it = std::find(order_.begin(), order_.end(), page);
    return it == order_.end() ? wxNOT_FOUND : static_cast<int>(it - order_.begin());
}

void DocumentTabs::select(std::size_t index) {
    if (index >= order_.size()) return;
    selected_ = static_cast<int>(index);
    const int bookIndex = book_->FindPage(order_[index]);
    if (bookIndex != wxNOT_FOUND) book_->ChangeSelection(static_cast<std::size_t>(bookIndex));
    strip_->Refresh();
}

void DocumentTabs::selectRelative(int offset) {
    if (order_.empty() || selected_ == wxNOT_FOUND) return;
    const int count = static_cast<int>(order_.size());
    select(static_cast<std::size_t>((selected_ + offset + count) % count));
}

void DocumentTabs::setTitle(wxWindow* page, const wxString& title, bool dirty) {
    const int index = indexOf(page);
    if (index == wxNOT_FOUND) return;
    Tab& tab = tabs_[static_cast<std::size_t>(index)];
    if (tab.title == title && tab.dirty == dirty) return;
    tab.title = title;
    tab.dirty = dirty;
    strip_->Refresh();
}

void DocumentTabs::addTrailing(wxWindow* window) {
    header_->Add(window, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(8)));
    Layout();
}

void DocumentTabs::moveTab(std::size_t from, std::size_t to) {
    std::swap(order_[from], order_[to]);
    std::swap(tabs_[from], tabs_[to]);
    selected_ = static_cast<int>(to);
    strip_->Refresh();
}

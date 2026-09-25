#include "ui/Widgets.hpp"

#include "ui/Columns.hpp"

#include <wx/event.h>
#include <wx/menu.h>

#include <utility>

namespace git_tools {

VirtualList::VirtualList(wxWindow* parent, long style, const ColumnSet& columns,
                         TextFn text)
    : wxListView(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | style),
      columns_(&columns),
      text_(std::move(text)) {
    for (const ColumnDef& def : columns_->columns) {
        widths_.push_back(FromDIP(def.width));
    }
    ApplyColumnLayout();
}

void VirtualList::ApplyColumnLayout() {
    for (size_t i = 0; i < shown_.size(); ++i) {
        widths_[shown_[i]] = GetColumnWidth(static_cast<int>(i));
    }
    shown_.clear();
    for (const ColumnState& c : LoadColumnLayout(*columns_)) {
        if (c.shown) shown_.push_back(c.id);
    }

    const int existing = GetColumnCount();
    for (int i = existing - 1; i >= static_cast<int>(shown_.size()); --i) {
        DeleteColumn(i);
    }
    for (size_t i = 0; i < shown_.size(); ++i) {
        const ColumnDef& def = columns_->columns[shown_[i]];
        wxListItem column;
        column.SetText(def.name);
        column.SetAlign(def.right ? wxLIST_FORMAT_RIGHT : wxLIST_FORMAT_LEFT);
        column.SetWidth(widths_[shown_[i]]);
        if (static_cast<int>(i) < existing) {
            SetColumn(static_cast<int>(i), column);
        } else {
            InsertColumn(static_cast<long>(i), column);
        }
    }
    Refresh();
}

wxString VirtualList::OnGetItemText(long item, long column) const {
    if (!text_ || column < 0 || static_cast<size_t>(column) >= shown_.size()) {
        return wxString();
    }
    return text_(item, static_cast<long>(shown_[static_cast<size_t>(column)]));
}

std::vector<long> SelectedRows(const wxListView* list) {
    std::vector<long> rows;
    for (long i = list->GetFirstSelected(); i >= 0; i = list->GetNextSelected(i)) {
        rows.push_back(i);
    }
    return rows;
}

long SelectedIndexIn(const wxListView* list, size_t count) {
    const long i = list->GetFirstSelected();
    return (i >= 0 && static_cast<size_t>(i) < count) ? i : -1;
}

void SelectOnlyRow(wxListView* list, long row) {
    list->SetItemState(-1, 0, wxLIST_STATE_SELECTED);
    list->Select(row);
    list->Focus(row);
}

void SelectAllRows(wxListView* list) {
    list->SetItemState(-1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}

bool ContextMenuAnchor(wxListView* list, const wxContextMenuEvent& event,
                       wxPoint& at) {
    if (event.GetPosition() == wxDefaultPosition) {
        const long row = list->GetFirstSelected();
        wxRect rect;
        if (row < 0 || !list->GetItemRect(row, rect, wxLIST_RECT_LABEL)) {
            return false;
        }
        at = rect.GetBottomLeft();
        return true;
    }
    at = list->ScreenToClient(event.GetPosition());
    int  flags  = 0;
    long column = 0;
    const long hit = list->HitTest(at, flags, &column);
    if (hit < 0) return false;
    if (!list->IsSelected(hit)) SelectOnlyRow(list, hit);
    return true;
}

int ChooseFromMenu(wxWindow* owner, const wxPoint& at,
                   std::initializer_list<MenuEntry> entries) {
    wxMenu menu;
    for (const MenuEntry& entry : entries) {
        if (!entry.text) continue;
        menu.Append(entry.id, entry.text);
        menu.Enable(entry.id, entry.enabled);
    }
    const int id = owner->GetPopupMenuSelectionFromUser(menu, at);
    return id == wxID_NONE ? 0 : id;
}

wxTextCtrl* CreateReadOnlyText(wxWindow* parent, long extraStyle) {
    auto* edit = new wxTextCtrl(
        parent, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_NOHIDESEL | extraStyle);
    edit->Bind(wxEVT_KEY_DOWN, [edit](wxKeyEvent& event) {
        if (event.GetModifiers() == wxMOD_CONTROL && event.GetKeyCode() == 'A') {
            edit->SelectAll();
            return;
        }
        event.Skip();
    });
    edit->Bind(wxEVT_SET_FOCUS, [edit](wxFocusEvent& event) {
        event.Skip();
        edit->CallAfter([edit] {
            long from = 0;
            long to   = 0;
            edit->GetSelection(&from, &to);
            if (from == 0 && to > 0 && to == edit->GetLastPosition()) {
                edit->SetInsertionPoint(0);
                edit->ShowPosition(0);
            }
        });
    });
    return edit;
}

void SetReadOnlyText(wxTextCtrl* edit, const std::wstring& text) {
    edit->ChangeValue(text);
    edit->SetInsertionPoint(0);
    edit->ShowPosition(0);
}

}

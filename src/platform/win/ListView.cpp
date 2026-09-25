#include "ui/ListView.hpp"

#include "ui/Columns.hpp"

namespace git_tools {

namespace {

void SelectOnlyRow(wxListView* list, long row) {
    list->SetItemState(-1, 0, wxLIST_STATE_SELECTED);
    list->Select(row);
    list->Focus(row);
}

}

VirtualList::VirtualList(wxWindow* parent, bool multiple, const ColumnSet& columns,
                         TextFn text)
    : wxListView(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | (multiple ? 0 : wxLC_SINGLE_SEL)),
      columns_(&columns),
      text_(std::move(text)) {
    for (const ColumnDef& def : columns_->columns) {
        widths_.push_back(FromDIP(def.width));
    }
    ApplyColumnLayout();

    Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
        event.Skip();
        if (selected_) selected_();
    });
    Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
        event.Skip();
        if (activated_) activated_();
    });
    Bind(wxEVT_LIST_CACHE_HINT, [this](wxListEvent& event) {
        if (rowsNeeded_) rowsNeeded_(event.GetCacheTo());
    });
    Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent& event) {
        if (!contextMenu_) return;
        wxPoint at;
        if (event.GetPosition() == wxDefaultPosition) {
            if (!AnchorAtSelection(at)) return;
        } else {
            at = ScreenToClient(event.GetPosition());
            int        flags = 0;
            const long hit   = HitTest(at, flags);
            if (hit < 0) return;
            if (!IsSelected(hit)) SelectOnly(hit);
        }
        contextMenu_(at);
    });
}

void VirtualList::ApplyColumnLayout() {
    for (size_t i = 0; i < shown_.size(); ++i) {
        widths_[shown_[i]] = GetColumnWidth(static_cast<int>(i));
    }
    shown_ = VisibleColumns(*columns_);

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

size_t VirtualList::RowCount() const {
    return static_cast<size_t>(GetItemCount());
}

void VirtualList::SetRowCount(size_t count) {
    SetItemCount(static_cast<long>(count));
}

void VirtualList::ResetRows(size_t count) {
    DeleteAllItems();
    SetItemCount(static_cast<long>(count));
    Refresh();
}

void VirtualList::RefreshRow(long row) {
    RefreshItem(row);
}

long VirtualList::SelectedRow() const {
    return GetFirstSelected();
}

std::vector<long> VirtualList::SelectedRows() const {
    std::vector<long> rows;
    for (long i = GetFirstSelected(); i >= 0; i = GetNextSelected(i)) {
        rows.push_back(i);
    }
    return rows;
}

void VirtualList::SelectOnly(long row) {
    SelectOnlyRow(this, row);
}

void VirtualList::SelectAllRows() {
    SetItemState(-1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}

bool VirtualList::AnchorAtSelection(wxPoint& at) {
    const long row = GetFirstSelected();
    wxRect rect;
    if (row < 0 || !GetItemRect(row, rect, wxLIST_RECT_LABEL)) return false;
    at = rect.GetBottomLeft();
    return true;
}

CheckList::CheckList(wxWindow* parent, const wxSize& size)
    : wxListView(parent, wxID_ANY, wxDefaultPosition, size,
                 wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER) {
    EnableCheckBoxes();
    AppendColumn(L"Column");
    SetColumnWidth(0, GetClientSize().x);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        SetColumnWidth(0, GetClientSize().x);
    });
}

void CheckList::ClearRows() {
    DeleteAllItems();
}

void CheckList::AppendRow(const std::wstring& text, bool checked) {
    const long row = InsertItem(GetItemCount(), text);
    CheckItem(row, checked);
}

void CheckList::SetRow(long row, const std::wstring& text, bool checked) {
    SetItemText(row, text);
    CheckItem(row, checked);
}

bool CheckList::IsRowChecked(long row) const {
    return IsItemChecked(row);
}

long CheckList::SelectedRow() const {
    return GetFirstSelected();
}

void CheckList::SelectOnly(long row) {
    SelectOnlyRow(this, row);
}

}

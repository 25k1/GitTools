#include "ui/ListView.hpp"

#include "ui/Columns.hpp"
#include "ui/Widgets.hpp"

#include <gtk/gtk.h>

#include <algorithm>

namespace git_tools {

namespace {

void SelectOnlyItem(wxDataViewCtrl* view, const wxDataViewItem& item) {
    view->UnselectAll();
    view->Select(item);
    view->SetCurrentItem(item);
    view->EnsureVisible(item);
}

}

class VirtualList::Model : public wxDataViewVirtualListModel {
public:
    explicit Model(VirtualList& owner) : owner_(owner) {}

    void GetValueByRow(wxVariant& value, unsigned row, unsigned col) const override {
        owner_.NoteRowShown(row);
        value = owner_.text_ ? wxString(owner_.text_(static_cast<long>(row),
                                                     static_cast<long>(col)))
                             : wxString();
    }

    bool SetValueByRow(const wxVariant&, unsigned, unsigned) override {
        return false;
    }

private:
    VirtualList& owner_;
};

VirtualList::VirtualList(wxWindow* parent, bool multiple, const ColumnSet& columns,
                         TextFn text)
    : wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     multiple ? wxDV_MULTIPLE : wxDV_SINGLE),
      columns_(&columns),
      text_(std::move(text)) {
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(GtkGetTreeView()), FALSE);
    model_ = new Model(*this);
    AssociateModel(model_);
    model_->DecRef();

    for (const ColumnDef& def : columns_->columns) {
        widths_.push_back(FromDIP(def.width));
    }
    ApplyColumnLayout();

    Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
        event.Skip();
        if (selected_) selected_();
    });
    Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent& event) {
        event.Skip();
        if (activated_) activated_();
    });
    Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, [this](wxDataViewEvent& event) {
        if (!contextMenu_ || !event.GetItem().IsOk()) return;
        wxPoint at = event.GetPosition();
        if (at == wxDefaultPosition && !AnchorAtSelection(at)) return;
        contextMenu_(at);
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        const bool menuKey =
            IsKey(event, WXK_MENU) || IsKey(event, WXK_F10, wxMOD_SHIFT);
        if (!menuKey || !contextMenu_) {
            event.Skip();
            return;
        }
        wxPoint at;
        if (AnchorAtSelection(at)) contextMenu_(at);
    });
}

void VirtualList::ApplyColumnLayout() {
    for (unsigned i = 0; i < GetColumnCount(); ++i) {
        const wxDataViewColumn* column = GetColumn(i);
        const unsigned          id     = column->GetModelColumn();
        if (id < widths_.size() && column->GetWidth() > 0) {
            widths_[id] = column->GetWidth();
        }
    }
    ClearColumns();
    for (const size_t id : VisibleColumns(*columns_)) {
        const ColumnDef& def = columns_->columns[id];
        AppendTextColumn(def.name, static_cast<unsigned>(id),
                         wxDATAVIEW_CELL_INERT, widths_[id],
                         def.right ? wxALIGN_RIGHT : wxALIGN_LEFT,
                         wxDATAVIEW_COL_RESIZABLE);
    }
}

size_t VirtualList::RowCount() const {
    return model_->GetCount();
}

void VirtualList::SetRowCount(size_t count) {
    size_t current = model_->GetCount();
    if (count < current) {
        ResetRows(count);
        return;
    }
    for (; current < count; ++current) model_->RowAppended();
}

void VirtualList::ResetRows(size_t count) {
    hintPending_ = false;
    hintRow_     = -1;
    model_->Reset(static_cast<unsigned>(count));
}

void VirtualList::RefreshRow(long row) {
    if (row >= 0 && static_cast<size_t>(row) < model_->GetCount()) {
        model_->RowChanged(static_cast<unsigned>(row));
    }
}

long VirtualList::SelectedRow() const {
    const std::vector<long> rows = SelectedRows();
    return rows.empty() ? -1 : rows.front();
}

std::vector<long> VirtualList::SelectedRows() const {
    wxDataViewItemArray items;
    GetSelections(items);
    std::vector<long> rows;
    for (size_t i = 0; i < items.size(); ++i) {
        rows.push_back(static_cast<long>(model_->GetRow(items[i])));
    }
    std::ranges::sort(rows);
    return rows;
}

void VirtualList::SelectOnly(long row) {
    if (row < 0 || static_cast<size_t>(row) >= model_->GetCount()) return;
    SelectOnlyItem(this, model_->GetItem(static_cast<unsigned>(row)));
    if (selected_) selected_();
}

void VirtualList::SelectAllRows() {
    SelectAll();
}

bool VirtualList::AnchorAtSelection(wxPoint& at) {
    const long row = SelectedRow();
    if (row < 0) return false;
    const wxDataViewItem item = model_->GetItem(static_cast<unsigned>(row));
    wxRect rect = GetItemRect(item);
    if (rect.IsEmpty()) {
        EnsureVisible(item);
        rect = GetItemRect(item);
    }
    at = rect.IsEmpty() ? wxPoint(0, 0) : rect.GetBottomLeft();
    return true;
}

void VirtualList::NoteRowShown(unsigned row) {
    if (!rowsNeeded_) return;
    const long shown = static_cast<long>(row);
    if (hintPending_) {
        hintRow_ = std::max(hintRow_, shown);
        return;
    }
    hintPending_ = true;
    hintRow_     = shown;
    CallAfter([this] {
        if (!hintPending_) return;
        hintPending_ = false;
        if (rowsNeeded_) rowsNeeded_(hintRow_);
    });
}

CheckList::CheckList(wxWindow* parent, const wxSize& size)
    : wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, size,
                         wxDV_SINGLE | wxDV_NO_HEADER) {
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(GtkGetTreeView()), FALSE);
    AppendToggleColumn(L"Shown", wxDATAVIEW_CELL_ACTIVATABLE, FromDIP(40));
    AppendTextColumn(L"Column", wxDATAVIEW_CELL_INERT, size.x - FromDIP(48));
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        const long row = SelectedRow();
        if (!IsKey(event, WXK_SPACE) || row < 0) {
            event.Skip();
            return;
        }
        const unsigned index = static_cast<unsigned>(row);
        SetToggleValue(!GetToggleValue(index, 0), index, 0);
    });
}

void CheckList::ClearRows() {
    DeleteAllItems();
}

void CheckList::AppendRow(const std::wstring& text, bool checked) {
    wxVector<wxVariant> values;
    values.push_back(wxVariant(checked));
    values.push_back(wxVariant(wxString(text)));
    AppendItem(values);
}

void CheckList::SetRow(long row, const std::wstring& text, bool checked) {
    const unsigned index = static_cast<unsigned>(row);
    SetToggleValue(checked, index, 0);
    SetTextValue(text, index, 1);
}

bool CheckList::IsRowChecked(long row) const {
    return GetToggleValue(static_cast<unsigned>(row), 0);
}

long CheckList::SelectedRow() const {
    return GetSelectedRow();
}

void CheckList::SelectOnly(long row) {
    const wxDataViewItem item = RowToItem(static_cast<int>(row));
    if (item.IsOk()) SelectOnlyItem(this, item);
}

}

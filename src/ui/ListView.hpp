#pragma once

#ifdef _WIN32
#include <wx/listctrl.h>
#else
#include <wx/dataview.h>
#endif

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace git_tools {

struct ColumnSet;

#ifdef _WIN32
using VirtualListBase = wxListView;
using CheckListBase   = wxListView;
#else
using VirtualListBase = wxDataViewCtrl;
using CheckListBase   = wxDataViewListCtrl;
#endif

class VirtualList : public VirtualListBase {
public:
    using TextFn = std::function<std::wstring(long row, long column)>;

    VirtualList(wxWindow* parent, bool multiple, const ColumnSet& columns,
                TextFn text);

    void ApplyColumnLayout();

    size_t RowCount() const;
    void   SetRowCount(size_t count);
    void   ResetRows(size_t count);
    void   RefreshRow(long row);

    long              SelectedRow() const;
    std::vector<long> SelectedRows() const;
    void              SelectOnly(long row);
    void              SelectAllRows();

    void WhenSelected(std::function<void()> fn) { selected_ = std::move(fn); }
    void WhenActivated(std::function<void()> fn) { activated_ = std::move(fn); }
    void WhenRowsNeeded(std::function<void(long lastRow)> fn) {
        rowsNeeded_ = std::move(fn);
    }
    void WhenContextMenu(std::function<void(const wxPoint& at)> fn) {
        contextMenu_ = std::move(fn);
    }

private:
    bool AnchorAtSelection(wxPoint& at);

#ifdef _WIN32
    wxString OnGetItemText(long item, long column) const override;

    std::vector<size_t> shown_;
#else
    class Model;

    void NoteRowShown(unsigned row);

    Model* model_       = nullptr;
    bool   hintPending_ = false;
    long   hintRow_     = -1;
#endif

    const ColumnSet*                    columns_;
    TextFn                              text_;
    std::vector<int>                    widths_;
    std::function<void()>               selected_;
    std::function<void()>               activated_;
    std::function<void(long)>           rowsNeeded_;
    std::function<void(const wxPoint&)> contextMenu_;
};

class CheckList : public CheckListBase {
public:
    CheckList(wxWindow* parent, const wxSize& size);

    void ClearRows();
    void AppendRow(const std::wstring& text, bool checked);
    void SetRow(long row, const std::wstring& text, bool checked);
    bool IsRowChecked(long row) const;
    long SelectedRow() const;
    void SelectOnly(long row);
};

inline long RowWithin(long row, size_t count) {
    return row >= 0 && static_cast<size_t>(row) < count ? row : -1;
}

inline bool HasFocusWithin(const wxWindow* window) {
    const wxWindow* focus = wxWindow::FindFocus();
    return focus && (focus == window || focus->GetParent() == window);
}

}

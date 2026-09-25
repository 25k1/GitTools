#pragma once

#include <wx/listctrl.h>
#include <wx/textctrl.h>

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

class wxContextMenuEvent;

namespace git_tools {

struct ColumnSet;

class VirtualList : public wxListView {
public:
    using TextFn = std::function<std::wstring(long row, long column)>;

    VirtualList(wxWindow* parent, long style, const ColumnSet& columns,
                TextFn text);

    void ApplyColumnLayout();

protected:
    wxString OnGetItemText(long item, long column) const override;

private:
    const ColumnSet*    columns_;
    TextFn              text_;
    std::vector<size_t> shown_;
    std::vector<int>    widths_;
};

std::vector<long> SelectedRows(const wxListView* list);

long SelectedIndexIn(const wxListView* list, size_t count);

void SelectOnlyRow(wxListView* list, long row);

void SelectAllRows(wxListView* list);

bool ContextMenuAnchor(wxListView* list, const wxContextMenuEvent& event,
                       wxPoint& at);

struct MenuEntry {
    int            id;
    const wchar_t* text;
    bool           enabled = true;
};

int ChooseFromMenu(wxWindow* owner, const wxPoint& at,
                   std::initializer_list<MenuEntry> entries);

wxTextCtrl* CreateReadOnlyText(wxWindow* parent, long extraStyle = 0);

void SetReadOnlyText(wxTextCtrl* edit, const std::wstring& text);

}

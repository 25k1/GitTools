#pragma once

#include <wx/listctrl.h>
#include <wx/textctrl.h>

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

class wxContextMenuEvent;

namespace git_tools {

class VirtualList : public wxListView {
public:
    using TextFn = std::function<std::wstring(long row, long column)>;

    VirtualList(wxWindow* parent, long style, TextFn text);

protected:
    wxString OnGetItemText(long item, long column) const override;

private:
    TextFn text_;
};

struct ListColumn {
    const wchar_t* name;
    int            width;
    bool           right = false;
};

void AddColumns(wxListCtrl* list, std::initializer_list<ListColumn> columns);

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

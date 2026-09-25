#pragma once

#include <wx/textctrl.h>

#include <initializer_list>
#include <string>

namespace git_tools {

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

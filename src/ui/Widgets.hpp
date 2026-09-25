#pragma once

#include <wx/textctrl.h>

#include <functional>
#include <initializer_list>
#include <string>

class wxDialog;
class wxSizer;

namespace git_tools {

inline bool IsKey(const wxKeyEvent& event, int key, int modifiers = wxMOD_NONE) {
    return event.GetKeyCode() == key && event.GetModifiers() == modifiers;
}

void CloseOnEscape(wxWindow* window, std::function<void()> close);

struct MenuEntry {
    int            id;
    const wchar_t* text;
    bool           enabled = true;
};

int ChooseFromMenu(wxWindow* owner, const wxPoint& at,
                   std::initializer_list<MenuEntry> entries);

wxSizer* LabeledRow(wxWindow* label, wxWindow* control, int gap);

void FinishDialog(wxDialog& dialog, wxSizer* sizer, int gap);

wxTextCtrl* CreateReadOnlyText(wxWindow* parent, long extraStyle = 0);

void SetReadOnlyText(wxTextCtrl* edit, const std::wstring& text);

void MoveCaret(wxTextCtrl* edit, long position);

}

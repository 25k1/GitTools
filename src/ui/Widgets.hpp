#pragma once

#include <wx/textctrl.h>

#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

class wxCheckBox;
class wxDialog;
class wxMenu;
class wxSizer;

namespace git_tools {

inline bool IsKey(const wxKeyEvent& event, int key, int modifiers = wxMOD_NONE) {
    return event.GetKeyCode() == key && event.GetModifiers() == modifiers;
}

void BindKey(wxWindow* window, int key, int modifiers, std::function<void()> action,
             const wxEventTypeTag<wxKeyEvent>& type = wxEVT_KEY_DOWN);

struct MenuEntry {
    const wchar_t*        text = nullptr;
    std::function<void()> action;
    bool                  enabled   = true;
    bool                  checkable = false;
    bool                  checked   = false;
};

using MenuEntries = std::vector<MenuEntry>;

inline const MenuEntry kMenuSeparator{};

inline MenuEntries AppendGroup(MenuEntries entries, const MenuEntries& group) {
    entries.push_back(kMenuSeparator);
    entries.insert(entries.end(), group.begin(), group.end());
    return entries;
}

wxMenu* BuildMenu(std::span<const MenuEntry> entries, int firstId);

bool RunMenuEntry(std::span<const MenuEntry> entries, int index);

bool RunShortcut(const wxKeyEvent& event, std::span<const MenuEntry> entries);

void ShowPopupMenu(wxWindow* owner, const wxPoint& at,
                   std::span<const MenuEntry> entries);

inline void ShowPopupMenu(wxWindow* owner, const wxPoint& at,
                          std::initializer_list<MenuEntry> entries) {
    ShowPopupMenu(owner, at, std::span(entries.begin(), entries.size()));
}

wxSizer* LabeledRow(wxWindow* label, wxWindow* control, int gap);

wxCheckBox* CreateCheckBox(wxWindow* parent, const wchar_t* label, bool value);

void FinishDialog(wxDialog& dialog, wxSizer* sizer, int gap);

wxTextCtrl* CreateReadOnlyText(wxWindow* parent, long extraStyle = 0);

void SetReadOnlyText(wxTextCtrl* edit, const std::wstring& text);

void MoveCaret(wxTextCtrl* edit, long position);

std::pair<long, long> TextSelection(const wxTextCtrl* edit);

}

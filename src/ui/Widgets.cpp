#include "ui/Widgets.hpp"

#include <wx/event.h>
#include <wx/menu.h>


namespace git_tools {

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

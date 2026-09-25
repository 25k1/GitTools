#include "ui/Widgets.hpp"

#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/menu.h>
#include <wx/sizer.h>

#include <utility>

namespace git_tools {

void CloseOnEscape(wxWindow* window, std::function<void()> close) {
    window->Bind(wxEVT_CHAR_HOOK, [close = std::move(close)](wxKeyEvent& event) {
        if (IsKey(event, WXK_ESCAPE)) {
            close();
            return;
        }
        event.Skip();
    });
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

wxSizer* LabeledRow(wxWindow* label, wxWindow* control, int gap) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    row->Add(control, 1, wxALIGN_CENTER_VERTICAL);
    return row;
}

void FinishDialog(wxDialog& dialog, wxSizer* sizer, int gap) {
    sizer->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0,
               wxEXPAND | wxALL, gap);
    dialog.SetSizerAndFit(sizer);
    dialog.CentreOnParent();
}

wxTextCtrl* CreateReadOnlyText(wxWindow* parent, long extraStyle) {
    auto* edit = new wxTextCtrl(
        parent, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_NOHIDESEL | extraStyle);
    edit->Bind(wxEVT_KEY_DOWN, [edit](wxKeyEvent& event) {
        if (IsKey(event, 'A', wxMOD_CONTROL)) {
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
                MoveCaret(edit, 0);
            }
        });
    });
    return edit;
}

void SetReadOnlyText(wxTextCtrl* edit, const std::wstring& text) {
    edit->ChangeValue(text);
    MoveCaret(edit, 0);
}

void MoveCaret(wxTextCtrl* edit, long position) {
    edit->SetInsertionPoint(position);
    edit->ShowPosition(position);
}

}

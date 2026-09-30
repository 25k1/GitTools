#include "ui/Widgets.hpp"

#include <wx/accel.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/menu.h>
#include <wx/sizer.h>

#include <cwchar>
#include <memory>
#include <utility>

namespace git_tools {

void BindKey(wxWindow* window, int key, int modifiers, std::function<void()> action,
             const wxEventTypeTag<wxKeyEvent>& type) {
    window->Bind(type, [key, modifiers, action = std::move(action)](wxKeyEvent& event) {
        if (IsKey(event, key, modifiers)) action();
        else                              event.Skip();
    });
}

namespace {

constexpr int kPopupFirstId = wxID_HIGHEST + 2000;

bool MatchesShortcut(const wxKeyEvent& event, const MenuEntry& entry) {
    wxAcceleratorEntry accel;
    if (!entry.text || !std::wcschr(entry.text, L'\t') || !accel.FromString(entry.text)) {
        return false;
    }
    const int flags     = accel.GetFlags();
    const int modifiers = ((flags & wxACCEL_CTRL) ? wxMOD_CONTROL : 0) |
                          ((flags & wxACCEL_SHIFT) ? wxMOD_SHIFT : 0) |
                          ((flags & wxACCEL_ALT) ? wxMOD_ALT : 0);
    return IsKey(event, accel.GetKeyCode(), modifiers);
}

}

wxMenu* BuildMenu(std::span<const MenuEntry> entries, int firstId) {
    auto* menu = new wxMenu;
    for (const MenuEntry& entry : entries) {
        const int id = firstId++;
        if (!entry.text) {
            menu->AppendSeparator();
            continue;
        }
        if (entry.checkable) {
            menu->AppendCheckItem(id, entry.text);
            menu->Check(id, entry.checked);
        } else {
            menu->Append(id, entry.text);
        }
        menu->Enable(id, entry.enabled);
    }
    return menu;
}

bool RunMenuEntry(std::span<const MenuEntry> entries, int index) {
    if (index < 0 || static_cast<size_t>(index) >= entries.size() ||
        !entries[static_cast<size_t>(index)].action) {
        return false;
    }
    const std::function<void()> action = entries[static_cast<size_t>(index)].action;
    action();
    return true;
}

bool RunShortcut(const wxKeyEvent& event, std::span<const MenuEntry> entries) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (MatchesShortcut(event, entries[i])) {
            return RunMenuEntry(entries, static_cast<int>(i));
        }
    }
    return false;
}

void ShowPopupMenu(wxWindow* owner, const wxPoint& at,
                   std::span<const MenuEntry> entries) {
    const std::unique_ptr<wxMenu> menu(BuildMenu(entries, kPopupFirstId));
    RunMenuEntry(entries, owner->GetPopupMenuSelectionFromUser(*menu, at) - kPopupFirstId);
}

wxSizer* LabeledRow(wxWindow* label, wxWindow* control, int gap) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    row->Add(control, 1, wxALIGN_CENTER_VERTICAL);
    return row;
}

wxCheckBox* CreateCheckBox(wxWindow* parent, const wchar_t* label, bool value) {
    auto* box = new wxCheckBox(parent, wxID_ANY, label);
    box->SetValue(value);
    return box;
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
    BindKey(edit, 'A', wxMOD_CONTROL, [edit] { edit->SelectAll(); });
    edit->Bind(wxEVT_SET_FOCUS, [edit](wxFocusEvent& event) {
        event.Skip();
        edit->CallAfter([edit] {
            const auto [from, to] = TextSelection(edit);
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

std::pair<long, long> TextSelection(const wxTextCtrl* edit) {
    long from = 0;
    long to   = 0;
    edit->GetSelection(&from, &to);
    return {from, to};
}

}

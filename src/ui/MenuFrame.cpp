#include "ui/MenuFrame.hpp"

#include "ui/OptionsDialog.hpp"

#include <wx/evtloop.h>
#include <wx/menu.h>
#include <wx/utils.h>

#include <utility>

namespace git_tools {

namespace {

constexpr int kFirstMenuId = wxID_HIGHEST + 1000;

}

MenuFrame::MenuFrame(wxWindow* parent, const std::wstring& title, const wxSize& size)
    : wxFrame(parent, wxID_ANY, title) {
    SetSize(FromDIP(size));
    Bind(wxEVT_MENU, [this](wxCommandEvent& event) {
        if (!RunMenuEntry(entries_, event.GetId() - kFirstMenuId)) event.Skip();
    });
    for (const auto type : {wxEVT_MENU_OPEN, wxEVT_MENU_CLOSE}) {
        Bind(type, [this, open = type == wxEVT_MENU_OPEN](wxMenuEvent& event) {
            event.Skip();
            if (!event.IsPopup()) SyncMenus(open);
        });
    }
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        EndModal();
        event.Skip();
    });
    BindKey(this, WXK_ESCAPE, wxMOD_NONE, [this] { Close(); }, wxEVT_CHAR_HOOK);
}

MenuFrame::~MenuFrame() = default;

void MenuFrame::RunModal() {
    wxGUIEventLoop loop;
    disabler_  = std::make_unique<wxWindowDisabler>(this);
    modalLoop_ = &loop;
    Show();
    loop.Run();
}

void MenuFrame::EndModal() {
    if (!modalLoop_) return;
    disabler_.reset();
    std::exchange(modalLoop_, nullptr)->Exit();
}

MenuEntry MenuFrame::OptionsEntry() {
    return {L"&Options...", [this] {
        if (ShowOptionsDialog(this)) OnOptionsChanged();
    }};
}

MenuEntry MenuFrame::CloseEntry(const wchar_t* text) {
    return {text, [this] { Close(); }};
}

MenuEntries MenuFrame::WithFileCommands(MenuEntries entries) {
    return AppendGroup(AppendGroup(std::move(entries), {OptionsEntry()}), {CloseEntry()});
}

void MenuFrame::BuildMenus() {
    auto* bar = new wxMenuBar;
    entries_.clear();
    for (MenuSection& section : Menus()) {
        const int firstId = kFirstMenuId + static_cast<int>(entries_.size());
        bar->Append(BuildMenu(section.entries, firstId), section.title);
        entries_.insert(entries_.end(), section.entries.begin(), section.entries.end());
    }
    SetMenuBar(bar);
}

void MenuFrame::SyncMenus(bool opening) {
    wxMenuBar* bar = GetMenuBar();
    if (!bar) return;
    int id = kFirstMenuId;
    for (const MenuSection& section : Menus()) {
        for (const MenuEntry& entry : section.entries) {
            const int item = id++;
            if (!entry.text) continue;
            bar->Enable(item, !opening || entry.enabled);
            if (entry.checkable) bar->Check(item, entry.checked);
        }
    }
}

}

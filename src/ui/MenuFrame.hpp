#pragma once

#include "ui/Widgets.hpp"

#include <wx/frame.h>

#include <memory>
#include <string>
#include <vector>

class wxGUIEventLoop;
class wxWindowDisabler;

namespace git_tools {

struct MenuSection {
    const wchar_t* title;
    MenuEntries    entries;
};

class MenuFrame : public wxFrame {
public:
    MenuFrame(wxWindow* parent, const std::wstring& title, const wxSize& size);
    ~MenuFrame() override;

    void RunModal();

protected:
    MenuEntry   OptionsEntry();
    MenuEntry   CloseEntry(const wchar_t* text = L"&Close");
    MenuEntries WithFileCommands(MenuEntries entries);

    void BuildMenus();
    bool HandleShortcut(const wxKeyEvent& event) { return RunShortcut(event, entries_); }

    virtual std::vector<MenuSection> Menus() = 0;
    virtual void OnOptionsChanged() {}

private:
    void SyncMenus(bool opening);
    void EndModal();

    MenuEntries                       entries_;
    wxGUIEventLoop*                   modalLoop_ = nullptr;
    std::unique_ptr<wxWindowDisabler> disabler_;
};

}

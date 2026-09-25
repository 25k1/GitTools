#pragma once

#include <functional>
#include <string>

class wxTopLevelWindow;
class wxWindow;

namespace git_tools {

int RunGui(std::function<int()> start);

void ShowOnActiveDisplay(wxTopLevelWindow* window);

void ForceForeground(wxTopLevelWindow* window);

void ShowError(wxWindow* parent, const std::wstring& title,
               const std::wstring& text);

void ShowInfo(wxWindow* parent, const std::wstring& title,
              const std::wstring& text);

inline void ShowCouldNotOpen(wxWindow* parent, const std::wstring& title,
                             const std::wstring& path) {
    ShowError(parent, title, L"Could not open:\n\n" + path);
}

bool SetClipboardText(const std::wstring& text);

}

#pragma once

#include <functional>
#include <string>

class wxTopLevelWindow;
class wxWindow;

namespace git_tools {

int RunGui(std::function<int()> start);

void ShowOnActiveDisplay(wxTopLevelWindow* window);

void ShowError(wxWindow* parent, const std::wstring& title,
               const std::wstring& text);

int RunGuiInfo(const std::wstring& title, const std::wstring& text);

int RunGuiError(const std::wstring& title, const std::wstring& text);

void RevealFile(wxWindow* parent, const std::wstring& path);

void EditFile(wxWindow* parent, const std::wstring& path, int line = 0);

bool SetClipboardText(const std::wstring& text);

}

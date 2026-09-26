#pragma once

#include <string>

class wxWindow;

namespace git_tools {

void PrepareAnnouncements(wxWindow* window);

void Announce(wxWindow* window, const std::wstring& text);

}

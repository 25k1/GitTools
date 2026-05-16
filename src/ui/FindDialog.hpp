#pragma once

#include <windows.h>

#include <string>

namespace git_tools {

struct FindParams {
    std::wstring what;
    bool         matchCase  = false;
    bool         wrapAround = false;
};

bool ShowFindDialog(HWND owner, FindParams& params);

}

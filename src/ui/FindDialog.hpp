#pragma once

#include <string>

class wxWindow;

namespace git_tools {

struct FindParams {
    std::wstring what;
    bool         matchCase  = false;
    bool         wrapAround = false;
};

bool ShowFindDialog(wxWindow* owner, FindParams& params);

}

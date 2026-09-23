#pragma once

#include <string>

class wxWindow;

namespace git_tools {

struct DiffWindowParams {
    std::wstring title;
    std::wstring diffText;
    std::wstring workTree;
};

void ShowDiffWindow(wxWindow* owner, const DiffWindowParams& params);

}

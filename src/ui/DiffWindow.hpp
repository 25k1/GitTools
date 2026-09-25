#pragma once

#include <string>
#include <string_view>

class wxWindow;

namespace git_tools {

struct DiffWindowParams {
    std::wstring title;
    std::wstring diffText;
    std::wstring workTree;
};

std::wstring SeparateFileDiffs(std::wstring_view text);

void ShowDiffWindow(wxWindow* owner, const DiffWindowParams& params);

}

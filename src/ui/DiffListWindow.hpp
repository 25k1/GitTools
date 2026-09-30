#pragma once

#include <string>

namespace git_tools {

struct DiffListParams {
    std::wstring origin;
    std::wstring diffText;
    std::wstring workTree;
};

void ShowDiffList(DiffListParams params);

}

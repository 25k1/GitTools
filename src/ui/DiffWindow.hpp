#pragma once

#include <windows.h>

#include <string>

namespace git_tools {

struct DiffWindowParams {
    std::wstring title;
    std::wstring diffText;
    std::wstring workTree;
};

int ShowDiffWindow(HWND owner, const DiffWindowParams& params);

void ResetSoundCache();

}

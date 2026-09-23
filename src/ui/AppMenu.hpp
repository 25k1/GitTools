#pragma once

#include "ui/OptionsDialog.hpp"
#include "ui/OutputPane.hpp"

#include <windows.h>

namespace git_tools {

inline constexpr int kCmdDebugOutput = 6001;
inline constexpr int kCmdExit        = 6002;
inline constexpr int kCmdOptions     = 6003;

inline void AttachFileMenu(HWND hwnd) {
    HMENU file = CreatePopupMenu();
    if (!file) return;
    AppendMenuW(file,
                MF_STRING | (DebugOutputEnabled() ? MF_CHECKED : MF_UNCHECKED),
                kCmdDebugOutput, L"&Debug output");
    AppendMenuW(file, MF_STRING, kCmdOptions, L"&Options...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, kCmdExit, L"E&xit");

    HMENU bar = CreateMenu();
    if (!bar) {
        DestroyMenu(file);
        return;
    }
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    SetMenu(hwnd, bar);
    DrawMenuBar(hwnd);
}

inline bool HandleFileMenuCommand(HWND hwnd, WPARAM wParam) {
    switch (LOWORD(wParam)) {
        case kCmdExit:    EndDialog(hwnd, 0);       return true;
        case kCmdOptions: ShowOptionsDialog(hwnd);  return true;
        default:          return false;
    }
}

inline void ToggleDebugOutput(HWND hwnd, OutputPane& out, int editId) {
    const bool on = !DebugOutputEnabled();
    SetDebugOutput(on);
    CheckMenuItem(GetMenu(hwnd), kCmdDebugOutput,
                  MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    out.SetVisible(hwnd, editId, on);
}

}

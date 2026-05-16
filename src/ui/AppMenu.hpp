#pragma once

#include "ui/OptionsDialog.hpp"
#include "ui/OutputPane.hpp"

#include <windows.h>

namespace git_tools {

constexpr int kCmdDebugOutput     = 6001;
constexpr int kCmdExit            = 6002;
constexpr int kCmdOptions         = 6003;

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
    if (LOWORD(wParam) == kCmdExit) {
        EndDialog(hwnd, 0);
        return true;
    }
    if (LOWORD(wParam) == kCmdOptions) {
        ShowOptionsDialog(hwnd);
        return true;
    }
    return false;
}

inline void CheckDebugMenu(HWND hwnd, bool on) {
    CheckMenuItem(GetMenu(hwnd), kCmdDebugOutput,
                  MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
}

}

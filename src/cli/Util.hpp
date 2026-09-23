#pragma once

#include "git/Process.hpp"
#include "ui/Encoding.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace git_tools {

inline bool SpawnDetachedProcess(const std::wstring& cwd,
                                 const std::wstring& cmdLine) {
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        nullptr, buf.data(),
        nullptr, nullptr, FALSE,
        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
        nullptr,
        cwd.empty() ? nullptr : cwd.c_str(),
        &si, &pi);
    if (!ok) return false;
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

inline void WriteConsoleLine(HANDLE h, const std::wstring& s) {
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
    std::wstring line = s;
    line += L"\r\n";
    DWORD written = 0;
    DWORD mode    = 0;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, line.data(),
                      static_cast<DWORD>(line.size()), &written, nullptr);
    } else {
        const std::string bytes = WideToUtf8(line);
        WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()),
                  &written, nullptr);
    }
}

}

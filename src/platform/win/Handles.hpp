#pragma once

#include <windows.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace git_tools {

inline bool WriteAll(HANDLE h, std::string_view data) {
    for (size_t offset = 0; offset < data.size();) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(data.size() - offset, size_t{1} << 20));
        DWORD written = 0;
        if (!WriteFile(h, data.data() + offset, chunk, &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

template <typename F>
void ReadAll(HANDLE h, F&& consume) {
    char  buf[65536];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) {
        consume(std::string_view(buf, got));
    }
}

inline std::wstring FormatLastError(DWORD code) {
    LPWSTR msg = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::wstring result = n > 0 && msg ? std::wstring(msg, n)
                                       : L"error " + std::to_wstring(code);
    if (msg) LocalFree(msg);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) {
        result.pop_back();
    }
    return result;
}

}

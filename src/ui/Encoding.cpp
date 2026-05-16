#include "ui/Encoding.hpp"

#include <windows.h>

namespace git_tools {

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.data(), static_cast<int>(utf8.size()),
        nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.data(), static_cast<int>(utf8.size()),
        out.data(), len);
    return out;
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        out.data(), len, nullptr, nullptr);
    return out;
}

}

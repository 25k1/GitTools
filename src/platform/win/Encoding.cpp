#include "util/Encoding.hpp"

#include <windows.h>

namespace git_tools {

std::wstring Utf8ToWide(std::string_view utf8) {
    const int size = static_cast<int>(utf8.size());
    const int len  = size ? MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size,
                                                nullptr, 0)
                          : 0;
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, out.data(), len);
    return out;
}

std::string WideToUtf8(std::wstring_view wide) {
    const int size = static_cast<int>(wide.size());
    const int len  = size ? WideCharToMultiByte(CP_UTF8, 0, wide.data(), size,
                                                nullptr, 0, nullptr, nullptr)
                          : 0;
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, out.data(), len,
                        nullptr, nullptr);
    return out;
}

std::wstring ToLower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

}

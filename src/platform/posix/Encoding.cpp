#include "util/Encoding.hpp"

#include <cwctype>

namespace git_tools {

static_assert(sizeof(wchar_t) == 4, "POSIX builds expect UTF-32 wchar_t");

namespace {

constexpr char32_t kReplacement = 0xFFFD;

bool IsContinuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

}

std::wstring Utf8ToWide(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    const auto* p   = reinterpret_cast<const unsigned char*>(utf8.data());
    const auto* end = p + utf8.size();
    while (p < end) {
        const unsigned char lead = *p;
        int      extra = 0;
        char32_t cp    = 0;
        char32_t min   = 0;
        if (lead < 0x80)                { cp = lead;        extra = 0; }
        else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; extra = 1; min = 0x80; }
        else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; extra = 2; min = 0x800; }
        else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; extra = 3; min = 0x10000; }
        else {
            out += static_cast<wchar_t>(kReplacement);
            ++p;
            continue;
        }
        if (end - p <= extra) {
            out += static_cast<wchar_t>(kReplacement);
            break;
        }
        bool valid = true;
        for (int i = 1; i <= extra; ++i) {
            if (!IsContinuation(p[i])) {
                valid = false;
                extra = i - 1;
                break;
            }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        if (!valid || cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            cp = kReplacement;
        }
        out += static_cast<wchar_t>(cp);
        p += extra + 1;
    }
    return out;
}

std::string WideToUtf8(std::wstring_view wide) {
    std::string out;
    out.reserve(wide.size());
    for (wchar_t w : wide) {
        char32_t cp = static_cast<char32_t>(w);
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

std::wstring ToLower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    return s;
}

}

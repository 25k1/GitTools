#pragma once

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace git_tools {

namespace text_detail {

template <typename C>
std::basic_string<C> TrimRight(std::basic_string_view<C> s) {
    constexpr C kSpace[] = {C(' '), C('\t'), C('\r'), C('\n'), C(0)};
    const size_t end = s.find_last_not_of(kSpace);
    return std::basic_string<C>(s.substr(0, end == s.npos ? 0 : end + 1));
}

}

template <typename C>
bool ParseDigits(std::basic_string_view<C> s, long long& value) {
    constexpr long long kCap = 1LL << 40;
    value = 0;
    for (C c : s) {
        if (c < C('0') || c > C('9')) return false;
        value = std::min(value * 10 + (c - C('0')), kCap);
    }
    return !s.empty();
}

inline bool ParseDigits(std::string_view s, long long& value) {
    return ParseDigits<char>(s, value);
}

inline bool ParseDigits(std::wstring_view s, long long& value) {
    return ParseDigits<wchar_t>(s, value);
}

inline bool IsOneOf(std::wstring_view s, std::span<const std::wstring_view> set) {
    return std::ranges::find(set, s) != set.end();
}

inline bool StartsWithAny(std::wstring_view s,
                          std::span<const std::wstring_view> prefixes) {
    return std::ranges::any_of(prefixes, [s](std::wstring_view p) {
        return s.starts_with(p);
    });
}

inline std::vector<std::wstring> Concat(std::vector<std::wstring> head,
                                        const std::vector<std::wstring>& tail) {
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
}

inline std::string TrimRight(std::string_view s) {
    return text_detail::TrimRight(s);
}

inline std::wstring TrimRight(std::wstring_view s) {
    return text_detail::TrimRight(s);
}

inline std::wstring Trim(std::wstring_view s) {
    const size_t begin = s.find_first_not_of(L" \t\r\n");
    return begin == s.npos ? std::wstring() : TrimRight(s.substr(begin));
}

template <typename F>
void ForEachLine(std::wstring_view text, F&& fn) {
    for (size_t pos = 0;;) {
        const size_t eol = text.find(L'\n', pos);
        std::wstring_view line = text.substr(pos, eol == text.npos
                                                      ? text.npos
                                                      : eol - pos);
        if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
        if constexpr (std::is_same_v<std::invoke_result_t<F&, std::wstring_view>,
                                     bool>) {
            if (!fn(line)) return;
        } else {
            fn(line);
        }
        if (eol == text.npos) return;
        pos = eol + 1;
    }
}

inline std::vector<std::wstring> SplitLines(std::wstring_view text) {
    std::vector<std::wstring> lines;
    ForEachLine(text, [&](std::wstring_view line) { lines.emplace_back(line); });
    return lines;
}

inline std::vector<size_t> WrapPoints(std::wstring_view line, size_t width) {
    std::vector<size_t> starts{0};
    if (width == 0) return starts;
    for (size_t start = 0; line.size() - start > width;) {
        const size_t space = line.rfind(L' ', start + width - 1);
        const size_t text  = line.find_first_not_of(L" \t", start);
        const bool   word  = space != line.npos && space > start && text < space;
        start = word ? space + 1 : start + width;
        starts.push_back(start);
    }
    return starts;
}

inline std::vector<std::wstring> Split(std::wstring_view s, wchar_t delim) {
    std::vector<std::wstring> out;
    for (size_t start = 0;;) {
        const size_t end = s.find(delim, start);
        out.emplace_back(s.substr(start, end == s.npos ? s.npos : end - start));
        if (end == s.npos) return out;
        start = end + 1;
    }
}

inline std::wstring Join(const std::vector<std::wstring>& parts,
                         std::wstring_view separator) {
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += separator;
        out += parts[i];
    }
    return out;
}

inline std::wstring NormalizeCRLF(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size() + s.size() / 32);
    for (wchar_t c : s) {
        if (c == L'\n')      out += L"\r\n";
        else if (c != L'\r') out += c;
    }
    return out;
}

inline std::wstring NormalizeLF(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        if (c != L'\r') out += c;
    }
    return out;
}

inline std::wstring NativeLineEnds(std::wstring_view s) {
#ifdef _WIN32
    return NormalizeCRLF(s);
#else
    return NormalizeLF(s);
#endif
}

}

#pragma once

#include <string>
#include <vector>

namespace git_tools {

inline void AppendQuotedArg(std::wstring& out, const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        out += arg;
        return;
    }
    out += L'"';
    for (size_t i = 0; i < arg.size(); ++i) {
        size_t bs = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++bs; ++i; }
        if (i == arg.size()) { out.append(bs * 2, L'\\'); break; }
        out.append(arg[i] == L'"' ? bs * 2 + 1 : bs, L'\\');
        out += arg[i];
    }
    out += L'"';
}

inline std::wstring BuildCommandLine(const std::wstring& executable,
                                     const std::vector<std::wstring>& args) {
    std::wstring cmd;
    AppendQuotedArg(cmd, executable);
    for (const std::wstring& a : args) {
        cmd += L' ';
        AppendQuotedArg(cmd, a);
    }
    return cmd;
}

}

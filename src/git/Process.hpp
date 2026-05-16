#pragma once

#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

namespace git_tools {

inline std::wstring CurrentDirectory() {
    DWORD len = GetCurrentDirectoryW(0, nullptr);
    if (len == 0) return {};
    std::wstring out(len, L'\0');
    DWORD n = GetCurrentDirectoryW(len, out.data());
    out.resize(n);
    return out;
}

inline std::string RStrip(std::string s) {
    while (!s.empty()) {
        char c = s.back();
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') s.pop_back();
        else break;
    }
    return s;
}

inline void AppendQuotedArg(std::wstring& out, const std::wstring& arg) {
    if (!arg.empty() &&
        arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        out += arg;
        return;
    }
    out += L'"';
    for (size_t i = 0; i < arg.size(); ) {
        size_t bs = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++bs; ++i; }
        if (i == arg.size()) { out.append(bs * 2, L'\\'); break; }
        if (arg[i] == L'"') {
            out.append(bs * 2 + 1, L'\\');
            out += arg[i];
        } else {
            out.append(bs, L'\\');
            out += arg[i];
        }
        ++i;
    }
    out += L'"';
}

enum class StdioMode {
    Capture,
    Inherit,
};

struct ProcessResult {
    bool         started     = false;
    int          exitCode    = -1;
    std::string  stdoutText;
    std::string  stderrText;
    std::wstring errorMessage;
};

class ProcessCanceller {
public:
    void Cancel() {
        std::lock_guard<std::mutex> lock(mu_);
        cancelled_ = true;
        if (process_) TerminateProcess(process_, 1);
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(mu_);
        cancelled_ = false;
    }

    bool Cancelled() {
        std::lock_guard<std::mutex> lock(mu_);
        return cancelled_;
    }

    bool Attach(HANDLE process) {
        std::lock_guard<std::mutex> lock(mu_);
        if (cancelled_) return false;
        process_ = process;
        return true;
    }

    void Detach() {
        std::lock_guard<std::mutex> lock(mu_);
        process_ = nullptr;
    }

private:
    std::mutex mu_;
    HANDLE     process_   = nullptr;
    bool       cancelled_ = false;
};

ProcessResult RunProcess(const std::wstring& executable,
                         const std::vector<std::wstring>& args,
                         const std::wstring& cwd,
                         StdioMode stdio = StdioMode::Capture,
                         ProcessCanceller* cancel = nullptr);

}

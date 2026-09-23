#pragma once

#include <windows.h>

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace git_tools {

inline std::wstring CurrentDirectory() {
    DWORD len = GetCurrentDirectoryW(0, nullptr);
    if (len == 0) return {};
    std::wstring out(len, L'\0');
    out.resize(GetCurrentDirectoryW(len, out.data()));
    return out;
}

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

enum class StdioMode {
    Capture,
    Inherit,
};

struct ProcessResult {
    bool         started  = false;
    int          exitCode = -1;
    std::string  stdoutText;
    std::string  stderrText;
    std::wstring errorMessage;

    bool ok() const { return started && exitCode == 0; }
};

class ProcessCanceller {
public:
    void Cancel() {
        std::lock_guard lock(mu_);
        cancelled_ = true;
        if (process_) TerminateProcess(process_, 1);
    }

    void Reset() {
        std::lock_guard lock(mu_);
        cancelled_ = false;
    }

    bool Cancelled() {
        std::lock_guard lock(mu_);
        return cancelled_;
    }

    bool Attach(HANDLE process) {
        std::lock_guard lock(mu_);
        if (cancelled_) return false;
        process_ = process;
        return true;
    }

    void Detach() {
        std::lock_guard lock(mu_);
        process_ = nullptr;
    }

private:
    std::mutex mu_;
    HANDLE     process_   = nullptr;
    bool       cancelled_ = false;
};

using OutputSink = std::function<void(std::string_view)>;

ProcessResult RunProcess(const std::wstring& executable,
                         const std::vector<std::wstring>& args,
                         const std::wstring& cwd,
                         StdioMode stdio = StdioMode::Capture,
                         ProcessCanceller* cancel = nullptr,
                         const OutputSink& onStdout = {},
                         const std::string* input = nullptr);

}

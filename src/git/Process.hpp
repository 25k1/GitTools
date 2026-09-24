#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace git_tools {

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

using ProcessId = std::intptr_t;

void KillProcess(ProcessId process);

class ProcessCanceller {
public:
    void Cancel() {
        std::lock_guard lock(mu_);
        cancelled_ = true;
        if (attached_) KillProcess(process_);
    }

    void Reset() {
        std::lock_guard lock(mu_);
        cancelled_ = false;
    }

    bool Cancelled() {
        std::lock_guard lock(mu_);
        return cancelled_;
    }

    bool Attach(ProcessId process) {
        std::lock_guard lock(mu_);
        if (cancelled_) return false;
        process_  = process;
        attached_ = true;
        return true;
    }

    void Detach() {
        std::lock_guard lock(mu_);
        attached_ = false;
    }

private:
    std::mutex mu_;
    ProcessId  process_   = 0;
    bool       attached_  = false;
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

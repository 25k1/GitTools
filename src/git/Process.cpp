#include "git/Process.hpp"

#include <windows.h>

#include <thread>
#include <vector>

namespace git_tools {

namespace {

std::wstring BuildCommandLine(const std::wstring& executable,
                              const std::vector<std::wstring>& args) {
    std::wstring cmd;
    AppendQuotedArg(cmd, executable);
    for (const auto& a : args) {
        cmd += L' ';
        AppendQuotedArg(cmd, a);
    }
    return cmd;
}

struct PipePair {
    HANDLE readEnd  = nullptr;
    HANDLE writeEnd = nullptr;
    ~PipePair() {
        if (readEnd)  CloseHandle(readEnd);
        if (writeEnd) CloseHandle(writeEnd);
    }
};

bool CreateInheritablePipe(PipePair& p) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&p.readEnd, &p.writeEnd, &sa, 0)) return false;
    if (!SetHandleInformation(p.readEnd, HANDLE_FLAG_INHERIT, 0)) return false;
    return true;
}

void DrainPipe(HANDLE h, std::string& out) {
    char buf[4096];
    DWORD bytesRead = 0;
    for (;;) {
        BOOL ok = ReadFile(h, buf, sizeof(buf), &bytesRead, nullptr);
        if (!ok || bytesRead == 0) break;
        out.append(buf, bytesRead);
    }
}

std::wstring FormatLastError(DWORD code) {
    LPWSTR msg = nullptr;
    DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0,
        reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::wstring result;
    if (n > 0 && msg) {
        result.assign(msg, n);
        while (!result.empty() &&
               (result.back() == L'\r' || result.back() == L'\n')) {
            result.pop_back();
        }
    } else {
        wchar_t buf[64];
        wsprintfW(buf, L"error %lu", code);
        result = buf;
    }
    if (msg) LocalFree(msg);
    return result;
}

}

ProcessResult RunProcess(const std::wstring& executable,
                         const std::vector<std::wstring>& args,
                         const std::wstring& cwd,
                         StdioMode stdio,
                         ProcessCanceller* cancel) {
    ProcessResult result;

    PipePair stdoutPipe, stderrPipe;
    HANDLE childStdin  = nullptr;
    HANDLE childStdout = nullptr;
    HANDLE childStderr = nullptr;

    if (stdio == StdioMode::Capture) {
        if (!CreateInheritablePipe(stdoutPipe) ||
            !CreateInheritablePipe(stderrPipe)) {
            result.errorMessage =
                L"failed to create pipes: " + FormatLastError(GetLastError());
            return result;
        }
        childStdout = stdoutPipe.writeEnd;
        childStderr = stderrPipe.writeEnd;
    } else {
        childStdin  = GetStdHandle(STD_INPUT_HANDLE);
        childStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        childStderr = GetStdHandle(STD_ERROR_HANDLE);
    }

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = childStdin;
    si.hStdOutput = childStdout;
    si.hStdError  = childStderr;

    PROCESS_INFORMATION pi{};

    std::wstring cmdLine = BuildCommandLine(executable, args);
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    LPCWSTR cwdPtr = cwd.empty() ? nullptr : cwd.c_str();

    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (stdio == StdioMode::Capture) flags |= CREATE_NO_WINDOW;

    BOOL ok = CreateProcessW(
        nullptr, cmdBuf.data(),
        nullptr, nullptr,
        TRUE,
        flags,
        nullptr,
        cwdPtr,
        &si, &pi);

    if (!ok) {
        result.errorMessage =
            L"CreateProcess failed: " + FormatLastError(GetLastError());
        return result;
    }
    result.started = true;

    if (cancel && !cancel->Attach(pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
    }

    if (stdio == StdioMode::Capture) {
        CloseHandle(stdoutPipe.writeEnd); stdoutPipe.writeEnd = nullptr;
        CloseHandle(stderrPipe.writeEnd); stderrPipe.writeEnd = nullptr;

        std::thread tOut([&] { DrainPipe(stdoutPipe.readEnd, result.stdoutText); });
        std::thread tErr([&] { DrainPipe(stderrPipe.readEnd, result.stderrText); });

        WaitForSingleObject(pi.hProcess, INFINITE);
        tOut.join();
        tErr.join();
    } else {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    if (cancel) cancel->Detach();

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
}

}

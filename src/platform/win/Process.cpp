#include "git/Process.hpp"

#include "platform/win/CommandLine.hpp"
#include "platform/win/Handles.hpp"

#include <thread>
#include <vector>

namespace git_tools {

namespace {

struct PipePair {
    HANDLE readEnd  = nullptr;
    HANDLE writeEnd = nullptr;
    ~PipePair() {
        if (readEnd)  CloseHandle(readEnd);
        if (writeEnd) CloseHandle(writeEnd);
    }
};

bool CreateInheritablePipe(PipePair& p, bool childReads = false) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&p.readEnd, &p.writeEnd, &sa, 0)) return false;
    HANDLE parentEnd = childReads ? p.writeEnd : p.readEnd;
    if (!SetHandleInformation(parentEnd, HANDLE_FLAG_INHERIT, 0)) return false;
    return true;
}

struct InheritList {
    std::vector<char>            buffer;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    HANDLE                       handles[3]{};

    ~InheritList() {
        if (list) DeleteProcThreadAttributeList(list);
    }

    bool Init(HANDLE in, HANDLE out, HANDLE err) {
        DWORD count = 0;
        for (HANDLE h : {in, out, err}) {
            if (h) handles[count++] = h;
        }
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        buffer.resize(size);
        auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            buffer.data());
        if (!InitializeProcThreadAttributeList(attrs, 1, 0, &size)) {
            return false;
        }
        list = attrs;
        return UpdateProcThreadAttribute(
                   list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                   handles, count * sizeof(HANDLE), nullptr, nullptr) != 0;
    }
};

void DrainPipe(HANDLE h, std::string& out, const OutputSink& sink = {}) {
    ReadAll(h, [&](std::string_view bytes) {
        if (sink) sink(bytes);
        else      out.append(bytes);
    });
}

}

void KillProcess(ProcessId process) {
    TerminateProcess(reinterpret_cast<HANDLE>(process), 1);
}

ProcessResult RunProcess(const std::wstring& executable,
                         const std::vector<std::wstring>& args,
                         const std::wstring& cwd,
                         StdioMode stdio,
                         ProcessCanceller* cancel,
                         const OutputSink& onStdout,
                         const std::string* input) {
    ProcessResult result;

    PipePair stdinPipe, stdoutPipe, stderrPipe;
    HANDLE childStdin  = nullptr;
    HANDLE childStdout = nullptr;
    HANDLE childStderr = nullptr;

    if (stdio == StdioMode::Capture) {
        if (!CreateInheritablePipe(stdoutPipe) ||
            !CreateInheritablePipe(stderrPipe) ||
            (input && !CreateInheritablePipe(stdinPipe, true))) {
            result.errorMessage =
                L"failed to create pipes: " + FormatLastError(GetLastError());
            return result;
        }
        childStdin  = stdinPipe.readEnd;
        childStdout = stdoutPipe.writeEnd;
        childStderr = stderrPipe.writeEnd;
    } else {
        childStdin  = GetStdHandle(STD_INPUT_HANDLE);
        childStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        childStderr = GetStdHandle(STD_ERROR_HANDLE);
    }

    STARTUPINFOEXW six{};
    STARTUPINFOW&  si = six.StartupInfo;
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
    InheritList inherit;
    if (stdio == StdioMode::Capture) {
        flags |= CREATE_NO_WINDOW;
        if (inherit.Init(childStdin, childStdout, childStderr)) {
            si.cb = sizeof(six);
            six.lpAttributeList = inherit.list;
            flags |= EXTENDED_STARTUPINFO_PRESENT;
        }
    }

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

    if (cancel && !cancel->Attach(reinterpret_cast<ProcessId>(pi.hProcess))) {
        TerminateProcess(pi.hProcess, 1);
    }

    if (stdio == StdioMode::Capture) {
        CloseHandle(stdoutPipe.writeEnd); stdoutPipe.writeEnd = nullptr;
        CloseHandle(stderrPipe.writeEnd); stderrPipe.writeEnd = nullptr;
        if (stdinPipe.readEnd) {
            CloseHandle(stdinPipe.readEnd);
            stdinPipe.readEnd = nullptr;
        }

        std::thread tIn;
        if (input) {
            tIn = std::thread([&] {
                WriteAll(stdinPipe.writeEnd, *input);
                CloseHandle(stdinPipe.writeEnd);
                stdinPipe.writeEnd = nullptr;
            });
        }
        std::thread tOut([&] {
            DrainPipe(stdoutPipe.readEnd, result.stdoutText, onStdout);
        });
        std::thread tErr([&] { DrainPipe(stderrPipe.readEnd, result.stderrText); });

        WaitForSingleObject(pi.hProcess, INFINITE);
        if (tIn.joinable()) tIn.join();
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

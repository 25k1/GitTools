#include "util/System.hpp"

#include "platform/win/CommandLine.hpp"
#include "platform/win/Handles.hpp"
#include "util/Encoding.hpp"

#include <iterator>

namespace git_tools {

namespace {

void WriteConsoleLine(DWORD stdHandle, const std::wstring& s) {
    HANDLE h = GetStdHandle(stdHandle);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
    const std::wstring line = s + L"\r\n";
    DWORD written = 0;
    DWORD mode    = 0;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, line.data(), static_cast<DWORD>(line.size()),
                      &written, nullptr);
    } else {
        WriteAll(h, WideToUtf8(line));
    }
}

}

std::wstring ExecutablePath() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf,
                                       static_cast<DWORD>(std::size(buf)));
    return (n == 0 || n >= std::size(buf)) ? std::wstring()
                                           : std::wstring(buf, n);
}

std::wstring CurrentDirectory() {
    DWORD len = GetCurrentDirectoryW(0, nullptr);
    if (len == 0) return {};
    std::wstring out(len, L'\0');
    out.resize(GetCurrentDirectoryW(len, out.data()));
    return out;
}

bool SpawnDetachedProcess(const std::wstring& cwd,
                          const std::wstring& executable,
                          const std::vector<std::wstring>& args) {
    const std::wstring cmdLine = BuildCommandLine(executable, args);
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
        return false;
    }
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

std::wstring LastSystemError() {
    return FormatLastError(GetLastError());
}

bool ReadStandardInput(std::string& bytes) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD  mode = 0;
    if (in == nullptr || in == INVALID_HANDLE_VALUE || GetConsoleMode(in, &mode)) {
        return false;
    }
    ReadAll(in, [&](std::string_view chunk) { bytes.append(chunk); });
    return true;
}

std::string ReadFileBytes(const std::wstring& path) {
    constexpr LONGLONG kMaxBytes = 32LL * 1024 * 1024;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};

    std::string out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
        size.QuadPart <= kMaxBytes) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        const BOOL ok = ReadFile(file, out.data(),
                                 static_cast<DWORD>(out.size()), &read, nullptr);
        out.resize(ok ? read : 0);
    }
    CloseHandle(file);

    if (out.starts_with("\xEF\xBB\xBF")) out.erase(0, 3);
    return out;
}

std::wstring WriteTempFile(std::string_view bytes) {
    wchar_t dir[MAX_PATH + 1];
    const DWORD n = GetTempPathW(static_cast<DWORD>(std::size(dir)), dir);
    if (n == 0 || n >= std::size(dir)) return {};
    wchar_t path[MAX_PATH];
    if (GetTempFileNameW(dir, L"gtd", 0, path) == 0) return {};

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY, nullptr);
    const bool ok = file != INVALID_HANDLE_VALUE && WriteAll(file, bytes);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!ok) {
        const DWORD error = GetLastError();
        DeleteFileW(path);
        SetLastError(error);
        return {};
    }
    return path;
}

void RemoveFile(const std::wstring& path) {
    DeleteFileW(path.c_str());
}

void UseUtf8Console() {
    SetConsoleOutputCP(CP_UTF8);
}

void ReleaseConsole() {
    FreeConsole();
}

void WriteOut(const std::wstring& s) {
    WriteConsoleLine(STD_OUTPUT_HANDLE, s);
}

void WriteErr(const std::wstring& s) {
    WriteConsoleLine(STD_ERROR_HANDLE, s);
}

}

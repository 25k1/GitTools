#include "cli/Detached.hpp"

#include "cli/Util.hpp"

#include <windows.h>

#include <iterator>
#include <string_view>

namespace git_tools {

bool IsDetachedInvocation(int argc, wchar_t** argv) {
    return argc >= 3 && std::wstring_view{argv[2]} == kDetachedFlag;
}

std::vector<std::wstring> ArgsFrom(int argc, wchar_t** argv, int first) {
    std::vector<std::wstring> out;
    for (int i = first; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

void ReportConsoleError(const std::wstring& msg) {
    WriteConsoleLine(GetStdHandle(STD_ERROR_HANDLE), msg);
}

void ReportDialogError(const std::wstring& title, const std::wstring& msg) {
    MessageBoxW(nullptr, msg.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

bool OpenRepoOrReport(const wchar_t* title, RepoContext& repo) {
    repo = OpenRepo();
    if (repo.ok()) return true;
    ReportDialogError(title, repo.errorMessage);
    return false;
}

int SpawnDetachedSelf(const std::wstring& subcommand,
                      const std::vector<std::wstring>& args) {
    wchar_t exePath[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(
        nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    if (n == 0 || n >= std::size(exePath)) {
        ReportConsoleError(L"Failed to resolve gittools.exe path.");
        return 1;
    }

    std::wstring cmd;
    AppendQuotedArg(cmd, exePath);
    cmd += L' ';
    cmd += subcommand;
    cmd += L' ';
    cmd += kDetachedFlag;
    for (const auto& a : args) {
        cmd += L' ';
        AppendQuotedArg(cmd, a);
    }

    if (!SpawnDetachedProcess(CurrentDirectory(), cmd)) {
        ReportConsoleError(
            L"Failed to spawn detached gittools process (error " +
            std::to_wstring(GetLastError()) + L").");
        return 1;
    }
    return 0;
}

}

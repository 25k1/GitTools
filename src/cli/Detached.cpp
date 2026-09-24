#include "cli/Detached.hpp"

#include "ui/App.hpp"
#include "util/System.hpp"

#include <string_view>

namespace git_tools {

bool IsDetachedInvocation(int argc, wchar_t** argv) {
    return argc >= 3 && std::wstring_view{argv[2]} == kDetachedFlag;
}

std::vector<std::wstring> ArgsFrom(int argc, wchar_t** argv, int first) {
    return first < argc ? std::vector<std::wstring>(argv + first, argv + argc)
                        : std::vector<std::wstring>();
}

bool OpenRepoOrReport(const wchar_t* title, RepoContext& repo) {
    repo = OpenRepo();
    if (!repo.ok()) ShowError(nullptr, title, repo.errorMessage);
    return repo.ok();
}

int SpawnDetachedSelf(const std::wstring& subcommand,
                      const std::vector<std::wstring>& args) {
    const std::wstring exe = ExecutablePath();
    if (exe.empty()) {
        WriteErr(L"Failed to resolve the gittools executable path.");
        return 1;
    }

    std::vector<std::wstring> full{subcommand, kDetachedFlag};
    full.insert(full.end(), args.begin(), args.end());
    if (!SpawnDetachedProcess(CurrentDirectory(), exe, full)) {
        WriteErr(L"Failed to spawn detached gittools process (" +
                 LastSystemError() + L").");
        return 1;
    }
    return 0;
}

}

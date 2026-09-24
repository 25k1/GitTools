#include "cli/PullLog.hpp"

#include "cli/Detached.hpp"
#include "cli/Log.hpp"
#include "git/Git.hpp"
#include "util/System.hpp"

namespace git_tools {

namespace {

std::wstring HeadSha(const std::wstring& cwd) {
    return TrimmedOutput(RunGit({L"rev-parse", L"HEAD"}, cwd));
}

}

int RunPullLog(int argc, wchar_t** argv) {
    if (IsDetachedInvocation(argc, argv) && argc >= 5) {
        return OpenLogWindow(L"gittools pull-log", L"",
                             RangeLogArgs(argv[3], argv[4]), LogErrors::Ignore);
    }

    UseUtf8Console();
    const std::wstring cwd    = CurrentDirectory();
    const std::wstring oldSha = HeadSha(cwd);

    std::vector<std::wstring> args = ArgsFrom(argc, argv, 2);
    args.insert(args.begin(), L"pull");
    ProcessResult pull = RunGit(args, cwd, StdioMode::Inherit);
    if (!pull.started) return 1;
    if (pull.exitCode != 0) return pull.exitCode;

    const std::wstring newSha = HeadSha(cwd);
    if (oldSha.empty() || newSha.empty() || oldSha == newSha) return 0;
    return SpawnDetachedSelf(L"pull-log", {oldSha, newSha});
}

}

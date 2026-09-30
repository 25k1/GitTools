#include "cli/DiffView.hpp"

#include "cli/Detached.hpp"
#include "git/Config.hpp"
#include "git/Git.hpp"
#include "util/System.hpp"

#include "ui/App.hpp"
#include "ui/DiffListWindow.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace git_tools {

namespace {

bool IsEscapeParameter(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x20 && u <= 0x3f;
}

std::string StripColorCodes(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && IsEscapeParameter(text[i])) ++i;
            continue;
        }
        out += text[i];
    }
    return out;
}

int ShowDiffFromFile(const std::wstring& path) {
    const std::string bytes = StripColorCodes(ReadFileBytes(path));
    RemoveFile(path);
    if (bytes.empty()) return 0;
    return RunGui([&] {
        const RepoContext repo = OpenRepo();
        DiffListParams    p;
        p.origin   = repo.ok() ? repo.root : std::wstring();
        p.diffText = Utf8ToWide(bytes);
        p.workTree = repo.ok() ? repo.workTree : CurrentDirectory();
        ShowDiffList(std::move(p));
        return 0;
    });
}

}

int RunDiffView(int argc, wchar_t** argv) {
    if (IsDetachedInvocation(argc, argv)) {
        return argc >= 4 ? ShowDiffFromFile(argv[3]) : 1;
    }

    std::string bytes;
    if (!ReadStandardInput(bytes)) {
        WriteErr(L"gittools diff-view shows a diff read from standard input, "
                 L"for example: git diff | gittools diff-view");
        return 1;
    }
    if (bytes.find_first_not_of(" \t\r\n") == std::string::npos) return 0;

    const std::wstring path = WriteTempFile(bytes);
    if (path.empty()) {
        WriteErr(L"Failed to write a temporary file (" + LastSystemError() + L").");
        return 1;
    }
    const int code = SpawnDetachedSelf(kDiffViewCommand, {path});
    if (code != 0) RemoveFile(path);
    return code;
}

int RunInstallDiffViewer() {
    if (!SetDiffViewer(true)) {
        WriteErr(L"Failed to set pager.diff in the global git config.");
        return 1;
    }
    WriteOut(L"git diff now opens in gittools when run in a terminal "
             L"(pager.diff, --global).");
    WriteOut(L"Remove with:  gittools uninstall-diff");
    return 0;
}

int RunUninstallDiffViewer() {
    if (!SetDiffViewer(false)) {
        WriteErr(L"Failed to unset pager.diff in the global git config.");
        return 1;
    }
    WriteOut(L"git diff no longer opens in gittools.");
    return 0;
}

}

#include "ui/Shell.hpp"

#include "git/Config.hpp"
#include "util/Encoding.hpp"
#include "util/System.hpp"
#include "util/Text.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string_view>
#include <vector>

namespace git_tools {

namespace {

struct EditorCacheData {
    bool                      resolved = false;
    std::wstring              path;
    std::vector<std::wstring> args;
};

EditorCacheData& EditorCache() {
    static EditorCacheData cache;
    return cache;
}

std::vector<std::wstring> SplitCommand(std::wstring_view command) {
    std::vector<std::wstring> parts;
    std::wstring current;
    bool    inToken = false;
    wchar_t quote   = 0;
    for (wchar_t c : command) {
        if (quote) {
            if (c == quote) quote = 0;
            else            current += c;
        } else if (c == L'"' || c == L'\'') {
            quote   = c;
            inToken = true;
        } else if (c == L' ' || c == L'\t') {
            if (inToken) parts.push_back(std::move(current));
            current.clear();
            inToken = false;
        } else {
            current += c;
            inToken = true;
        }
    }
    if (inToken) parts.push_back(std::move(current));
    return parts;
}

bool IsExecutable(const std::wstring& path) {
    return !path.empty() && access(WideToUtf8(path).c_str(), X_OK) == 0;
}

std::wstring FindInPath(const std::wstring& name) {
    if (name.find(L'/') != std::wstring::npos) {
        return IsExecutable(name) ? name : std::wstring();
    }
    const char* path = std::getenv("PATH");
    if (!path) return {};
    for (const std::wstring& dir : Split(Utf8ToWide(path), L':')) {
        if (dir.empty()) continue;
        std::wstring candidate = dir + L"/" + name;
        if (IsExecutable(candidate)) return candidate;
    }
    return {};
}

std::wstring BaseName(const std::wstring& path) {
    const size_t slash = path.rfind(L'/');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

template <size_t N>
bool OneOf(const std::wstring& name, const std::wstring_view (&names)[N]) {
    return std::ranges::find(names, name) != std::end(names);
}

bool AppendLineArgs(std::vector<std::wstring>& args, const std::wstring& editor,
                    const std::wstring& path, int line) {
    constexpr std::wstring_view kGoto[]     = {L"code", L"codium", L"code-oss",
                                               L"cursor"};
    constexpr std::wstring_view kSuffix[]   = {L"subl", L"sublime_text", L"zed"};
    constexpr std::wstring_view kPlus[]     = {L"gedit", L"geany", L"mousepad",
                                               L"pluma", L"xed", L"gvim",
                                               L"vim", L"nvim", L"emacs",
                                               L"emacsclient", L"nano"};
    constexpr std::wstring_view kLineFlag[] = {L"kate", L"kwrite"};

    const std::wstring name = BaseName(editor);
    const std::wstring num  = std::to_wstring(line);
    if (OneOf(name, kGoto)) {
        args.push_back(L"--goto");
        args.push_back(path + L":" + num);
    } else if (OneOf(name, kSuffix)) {
        args.push_back(path + L":" + num);
    } else if (OneOf(name, kPlus)) {
        args.push_back(L"+" + num);
        args.push_back(path);
    } else if (OneOf(name, kLineFlag)) {
        args.push_back(L"--line");
        args.push_back(num);
        args.push_back(path);
    } else {
        return false;
    }
    return true;
}

bool ReplaceAll(std::wstring& text, std::wstring_view from, const std::wstring& to) {
    bool replaced = false;
    for (size_t pos = text.find(from); pos != std::wstring::npos;
         pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
        replaced = true;
    }
    return replaced;
}

}

std::wstring RepoFilePath(const std::wstring& repoRoot,
                          const std::wstring& relativePath) {
    if (repoRoot.empty()) return {};
    std::wstring path = repoRoot;
    if (!path.ends_with(L'/')) path += L'/';
    return path + relativePath;
}

std::wstring ParentDirectory(const std::wstring& path) {
    const size_t slash = path.rfind(L'/');
    if (slash == std::wstring::npos) return {};
    return slash == 0 ? std::wstring(L"/") : path.substr(0, slash);
}

bool PathExists(const std::wstring& path) {
    struct stat info {};
    return !path.empty() && stat(WideToUtf8(path).c_str(), &info) == 0;
}

void ResetEditorCache() { EditorCache() = EditorCacheData(); }

std::wstring FindEditor() {
    EditorCacheData& cache = EditorCache();
    if (cache.resolved) return cache.path;
    cache.resolved = true;

    std::vector<std::wstring> parts = SplitCommand(Trim(ConfigGet(kEditorKey)));
    if (!parts.empty()) {
        if (std::wstring exe = FindInPath(parts.front()); !exe.empty()) {
            cache.args.assign(parts.begin() + 1, parts.end());
            return cache.path = exe;
        }
    }
    return cache.path = FindInPath(L"xdg-open");
}

bool RevealInExplorer(const std::wstring& path) {
    const std::wstring opener = FindInPath(L"xdg-open");
    const std::wstring folder = ParentDirectory(path);
    return !opener.empty() && !folder.empty() &&
           SpawnDetachedProcess(L"", opener, {folder});
}

bool OpenWithEditor(const std::wstring& editor, const std::wstring& path,
                    int line) {
    if (editor.empty()) return false;

    FindEditor();
    std::vector<std::wstring> args;
    bool lineUsed = false;
    bool pathUsed = false;
    for (std::wstring arg : EditorCache().args) {
        lineUsed = ReplaceAll(arg, L"%L", line > 0 ? std::to_wstring(line)
                                                   : std::wstring()) ||
                   lineUsed;
        pathUsed = ReplaceAll(arg, L"%1", path) || pathUsed;
        args.push_back(std::move(arg));
    }
    if (!pathUsed) {
        if (lineUsed || line <= 0 || !AppendLineArgs(args, editor, path, line)) {
            args.push_back(path);
        }
    }
    return SpawnDetachedProcess(L"", editor, args);
}

}

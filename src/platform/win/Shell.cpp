#include "ui/Shell.hpp"

#include "git/Config.hpp"
#include "util/Encoding.hpp"
#include "util/Text.hpp"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <vector>

namespace git_tools {

namespace {

struct EditorCacheData {
    bool         resolved = false;
    std::wstring path;
    std::wstring args;
};

EditorCacheData& EditorCache() {
    static EditorCacheData cache;
    return cache;
}

std::wstring ExpandEnv(const std::wstring& s) {
    if (s.find(L'%') == std::wstring::npos) return s;
    const DWORD n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (n == 0) return s;
    std::wstring out(n, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(s.c_str(), out.data(), n);
    if (written == 0 || written > n) return s;
    out.resize(written - 1);
    return out;
}

std::wstring ReadRegString(HKEY root, const wchar_t* subKey) {
    HKEY key = nullptr;
    for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE | view, &key) ==
            ERROR_SUCCESS) {
            break;
        }
        key = nullptr;
    }
    if (!key) return {};

    std::wstring out;
    DWORD type  = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &bytes) ==
            ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
        std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(key, nullptr, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buf.data()),
                             &bytes) == ERROR_SUCCESS) {
            out.assign(buf.data());
        }
    }
    RegCloseKey(key);
    return type == REG_EXPAND_SZ ? ExpandEnv(out) : out;
}

std::wstring ExtractExecutable(const std::wstring& command) {
    const size_t start = command.find_first_not_of(L' ');
    if (start == std::wstring::npos) return {};
    if (command[start] == L'"') {
        const size_t end = command.find(L'"', start + 1);
        return end == std::wstring::npos
                   ? std::wstring()
                   : command.substr(start + 1, end - start - 1);
    }
    return command.substr(start, command.find(L' ', start) - start);
}

std::wstring Quoted(const std::wstring& s) {
    return L"\"" + s + L"\"";
}

size_t LastSeparator(const std::wstring& path) {
    return path.find_last_of(L"\\/");
}

bool IsNotepadPlusPlus(const std::wstring& exe) {
    const size_t slash = LastSeparator(exe);
    return ToLower(slash == std::wstring::npos ? exe : exe.substr(slash + 1)) ==
           L"notepad++.exe";
}

void AppendArg(std::wstring& args, const std::wstring& arg) {
    if (!args.empty()) args += L' ';
    args += arg;
}

bool Launch(const std::wstring& file, const std::wstring& args) {
    HINSTANCE rc = ShellExecuteW(nullptr, nullptr, file.c_str(), args.c_str(),
                                 nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
}

}

std::wstring RepoFilePath(const std::wstring& repoRoot,
                          const std::wstring& relativePath) {
    if (repoRoot.empty()) return {};
    std::wstring path = repoRoot;
    if (!path.ends_with(L'/') && !path.ends_with(L'\\')) path += L'\\';
    path += relativePath;
    std::ranges::replace(path, L'/', L'\\');
    return path;
}

std::wstring ParentDirectory(const std::wstring& path) {
    const size_t slash = LastSeparator(path);
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool PathExists(const std::wstring& path) {
    return !path.empty() &&
           GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void ResetEditorCache() { EditorCache() = EditorCacheData(); }

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

std::wstring FindEditor() {
    EditorCacheData& cache = EditorCache();
    if (cache.resolved) return cache.path;
    cache.resolved = true;

    const std::wstring configured = Trim(ConfigGet(kEditorKey));
    if (!configured.empty()) {
        if (PathExists(configured)) return cache.path = configured;
        const std::wstring exe = ExtractExecutable(configured);
        if (!exe.empty() && PathExists(exe)) {
            size_t used = configured.find(exe);
            used = used == std::wstring::npos ? exe.size() : used + exe.size();
            if (used < configured.size() && configured[used] == L'"') ++used;
            cache.args = Trim(std::wstring_view(configured).substr(used));
            return cache.path = exe;
        }
    }

    const std::wstring candidates[] = {
        ExtractExecutable(ReadRegString(
            HKEY_CLASSES_ROOT,
            L"Applications\\notepad++.exe\\shell\\open\\command")),
        ReadRegString(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\notepad++.exe"),
        RepoFilePath(ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Notepad++"),
                     L"notepad++.exe"),
    };
    for (const std::wstring& exe : candidates) {
        if (PathExists(exe)) return cache.path = exe;
    }
    return cache.path;
}

bool RevealInExplorer(const std::wstring& path) {
    return Launch(L"explorer.exe", L"/select," + Quoted(path));
}

bool OpenWithEditor(const std::wstring& editor, const std::wstring& path,
                    int line) {
    if (editor.empty()) return false;

    FindEditor();
    std::wstring args = EditorCache().args;

    if (const size_t slot = args.find(L"%L"); slot != std::wstring::npos) {
        args.replace(slot, 2, line > 0 ? std::to_wstring(line) : std::wstring());
    } else if (line > 0 && IsNotepadPlusPlus(editor)) {
        AppendArg(args, L"-n" + std::to_wstring(line));
    }

    if (const size_t slot = args.find(L"%1"); slot != std::wstring::npos) {
        args.replace(slot, 2, Quoted(path));
    } else {
        AppendArg(args, Quoted(path));
    }
    return Launch(editor, args);
}

}

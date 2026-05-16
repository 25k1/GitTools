#include "ui/Shell.hpp"

#include "git/Config.hpp"

#include <shellapi.h>

#include <filesystem>
#include <system_error>
#include <vector>

namespace git_tools {

namespace {

std::wstring ExpandEnv(const std::wstring& s) {
    if (s.find(L'%') == std::wstring::npos) return s;
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (n == 0) return s;
    std::wstring out(n, L'\0');
    DWORD written = ExpandEnvironmentStringsW(s.c_str(), out.data(), n);
    if (written == 0 || written > n) return s;
    out.resize(written > 0 ? written - 1 : 0);
    return out;
}

std::wstring ReadRegString(HKEY root, const wchar_t* subKey,
                           const wchar_t* valueName) {
    HKEY key = nullptr;
    const REGSAM views[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    for (REGSAM view : views) {
        if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE | view, &key) ==
            ERROR_SUCCESS) {
            break;
        }
        key = nullptr;
    }
    if (!key) return {};

    std::wstring out;
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes) ==
            ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
        std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 1, L'\0');
        DWORD size = bytes;
        if (RegQueryValueExW(key, valueName, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buf.data()),
                             &size) == ERROR_SUCCESS) {
            out.assign(buf.data());
        }
    }
    RegCloseKey(key);

    if (type == REG_EXPAND_SZ) out = ExpandEnv(out);
    return out;
}

std::wstring ExtractExecutable(const std::wstring& command) {
    size_t start = command.find_first_not_of(L' ');
    if (start == std::wstring::npos) return {};
    if (command[start] == L'"') {
        size_t end = command.find(L'"', start + 1);
        if (end == std::wstring::npos) return {};
        return command.substr(start + 1, end - start - 1);
    }
    size_t end = command.find(L' ', start);
    return command.substr(start, end - start);
}

std::wstring Quoted(const std::wstring& s) {
    return L"\"" + s + L"\"";
}

bool IsNotepadPlusPlus(const std::wstring& exe) {
    std::wstring name = std::filesystem::path(exe).filename().wstring();
    if (!name.empty()) CharLowerBuffW(name.data(),
                                      static_cast<DWORD>(name.size()));
    return name == L"notepad++.exe";
}

std::wstring Trimmed(const std::wstring& s) {
    size_t first = s.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    size_t last = s.find_last_not_of(L" \t");
    return s.substr(first, last - first + 1);
}
}

std::wstring RepoFilePath(const std::wstring& repoRoot,
                          const std::wstring& relativePath) {
    std::filesystem::path full =
        std::filesystem::path(repoRoot) / relativePath;
    return full.make_preferred().wstring();
}

std::wstring ParentDirectory(const std::wstring& path) {
    return std::filesystem::path(path).parent_path().wstring();
}

bool PathExists(const std::wstring& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(path), ec);
}

struct EditorCacheData {
    bool         resolved = false;
    std::wstring path;
    std::wstring args;
};

EditorCacheData& EditorCache() {
    static EditorCacheData c;
    return c;
}

void ResetEditorCache() { EditorCache() = EditorCacheData(); }

std::string ReadFileBytes(const std::wstring& path) {
    constexpr DWORD kMaxBytes = 32u * 1024u * 1024u;

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
        if (!ReadFile(file, out.data(), static_cast<DWORD>(out.size()),
                      &read, nullptr)) {
            out.clear();
        } else {
            out.resize(read);
        }
    }
    CloseHandle(file);

    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return out;
}

std::wstring FindEditor() {
    EditorCacheData& cache = EditorCache();
    if (cache.resolved) return cache.path;
    cache.resolved = true;
    std::wstring& cached = cache.path;

    const std::wstring configured = Trimmed(ConfigGet(L"editor"));
    if (!configured.empty()) {
        if (PathExists(configured)) {
            cached = configured;
            return cached;
        }
        const std::wstring exe = ExtractExecutable(configured);
        if (!exe.empty() && PathExists(exe)) {
            cached = exe;
            size_t used = configured.find(exe);
            used = (used == std::wstring::npos)
                       ? exe.size()
                       : used + exe.size();
            if (used < configured.size() && configured[used] == L'"') ++used;
            cache.args = Trimmed(configured.substr(used));
            return cached;
        }
    }

    const std::wstring candidates[] = {
        ExtractExecutable(ReadRegString(
            HKEY_CLASSES_ROOT,
            L"Applications\\notepad++.exe\\shell\\open\\command", nullptr)),
        ReadRegString(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\notepad++.exe",
            nullptr),
        RepoFilePath(
            ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Notepad++", nullptr),
            L"notepad++.exe"),
    };
    for (const std::wstring& exe : candidates) {
        if (!exe.empty() && PathExists(exe)) {
            cached = exe;
            break;
        }
    }
    return cached;
}

bool RevealInExplorer(HWND owner, const std::wstring& path) {
    std::wstring args = L"/select," + Quoted(path);
    HINSTANCE rc = ShellExecuteW(owner, nullptr, L"explorer.exe", args.c_str(),
                                 nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
}

bool OpenWithEditor(HWND owner, const std::wstring& editor,
                    const std::wstring& path, int line) {
    if (editor.empty()) return false;

    FindEditor();
    std::wstring args = EditorCache().args;

    const size_t lineSlot = args.find(L"%L");
    if (lineSlot != std::wstring::npos) {
        args.replace(lineSlot, 2, line > 0 ? std::to_wstring(line)
                                           : std::wstring());
    } else if (line > 0 && IsNotepadPlusPlus(editor)) {
        if (!args.empty()) args += L' ';
        args += L"-n" + std::to_wstring(line);
    }

    const size_t slot = args.find(L"%1");
    if (slot == std::wstring::npos) {
        if (!args.empty()) args += L' ';
        args += Quoted(path);
    } else {
        args.replace(slot, 2, Quoted(path));
    }
    HINSTANCE rc = ShellExecuteW(owner, nullptr, editor.c_str(), args.c_str(),
                                 nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
}
}

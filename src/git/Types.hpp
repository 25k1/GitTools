#pragma once

#include <string>

namespace git_tools {

struct Commit {
    std::wstring fullSha;
    std::wstring shortSha;
    std::wstring author;
    std::wstring authorEmail;
    std::wstring date;
    std::wstring subject;
    std::wstring message;
};

enum class FileChangeKind {
    Modified,
    Added,
    Deleted,
    Renamed,
    Copied,
    TypeChanged,
    Other,
};

struct FileChange {
    FileChangeKind kind     = FileChangeKind::Other;
    wchar_t        kindChar = L'?';
    std::wstring   path;
    std::wstring   oldPath;
    int            insertions = -1;
    int            deletions  = -1;
};

struct Branch {
    bool         isCurrent = false;
    bool         isRemote  = false;
    std::wstring name;
    std::wstring upstream;
    std::wstring shortSha;
    std::wstring subject;
};

}

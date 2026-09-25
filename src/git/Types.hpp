#pragma once

#include <string>
#include <vector>

namespace git_tools {

struct Commit {
    std::wstring fullSha;
    std::wstring shortSha;
    std::wstring author;
    std::wstring authorEmail;
    std::wstring date;
    std::wstring subject;
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

inline bool HasOldPath(FileChangeKind kind) {
    return kind == FileChangeKind::Renamed || kind == FileChangeKind::Copied;
}

struct FileChange {
    FileChangeKind kind     = FileChangeKind::Other;
    wchar_t        kindChar = L'?';
    std::wstring   path;
    std::wstring   oldPath;
    int            insertions = -1;
    int            deletions  = -1;
};

struct CommitDetails {
    std::wstring            sha;
    std::wstring            message;
    std::vector<FileChange> changes;
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

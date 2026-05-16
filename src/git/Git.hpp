#pragma once

#include "git/Process.hpp"
#include "git/Types.hpp"
#include "ui/Encoding.hpp"

#include <string>
#include <vector>

namespace git_tools {

ProcessResult RunGit(const std::vector<std::wstring>& args,
                     const std::wstring& cwd = L"",
                     StdioMode stdio = StdioMode::Capture,
                     ProcessCanceller* cancel = nullptr);

inline std::wstring TrimmedOutput(const ProcessResult& r) {
    if (!r.started || r.exitCode != 0) return {};
    return Utf8ToWide(RStrip(r.stdoutText));
}

struct RepoContext {
    std::wstring cwd;
    std::wstring root;
    std::wstring errorMessage;

    bool ok() const { return errorMessage.empty(); }
};

RepoContext OpenRepo();

struct CommitListResult {
    std::vector<Commit> commits;
    std::wstring        errorMessage;
};

CommitListResult LoadCommitLog(const std::vector<std::wstring>& logArgs,
                               const std::wstring& cwd);

std::wstring CurrentBranchLabel(const std::wstring& cwd);

std::wstring QueryBranchLabel(const std::vector<std::wstring>& logArgs,
                              const std::wstring& cwd);

inline std::vector<std::wstring> RangeLogArgs(const std::wstring& oldSha,
                                              const std::wstring& newSha) {
    return {oldSha + L".." + newSha};
}

CommitListResult LoadCommitRange(const std::wstring& oldSha,
                                 const std::wstring& newSha,
                                 const std::wstring& cwd);

std::vector<FileChange> LoadCommitChanges(const std::wstring& sha,
                                          const std::wstring& cwd,
                                          ProcessCanceller* cancel = nullptr);

std::wstring LoadFilesDiff(const std::wstring& sha,
                           const std::vector<std::wstring>& paths,
                           const std::wstring& cwd);

struct BranchListResult {
    std::vector<Branch> branches;
    std::wstring        errorMessage;
};

BranchListResult LoadBranchList(const std::wstring& cwd);

ProcessResult CheckoutBranch(const std::wstring& name,
                             const std::wstring& cwd);

}

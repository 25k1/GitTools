#pragma once

#include "git/CommitStore.hpp"
#include "git/Process.hpp"
#include "git/Types.hpp"
#include "util/Encoding.hpp"
#include "util/Text.hpp"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace git_tools {

ProcessResult RunGit(const std::vector<std::wstring>& args,
                     const std::wstring& cwd = L"",
                     StdioMode stdio = StdioMode::Capture,
                     ProcessCanceller* cancel = nullptr,
                     const OutputSink& onStdout = {},
                     const std::string* input = nullptr);

inline std::wstring TrimmedOutput(const ProcessResult& r) {
    return r.ok() ? Utf8ToWide(TrimRight(r.stdoutText)) : std::wstring();
}

std::wstring GitFailure(const std::wstring& what, const ProcessResult& r);

struct RepoContext {
    std::wstring cwd;
    std::wstring root;
    std::wstring workTree;
    std::wstring errorMessage;

    bool ok() const { return errorMessage.empty(); }
};

RepoContext OpenRepo();

inline constexpr size_t kCommitPage = 500;

class CommitLoader {
public:
    CommitLoader(const std::vector<std::wstring>& logArgs,
                 const std::wstring& cwd);
    ~CommitLoader();

    CommitLoader(const CommitLoader&)            = delete;
    CommitLoader& operator=(const CommitLoader&) = delete;

    void Notify(std::function<void()> onCommits);
    void Request(size_t count);
    void WaitFor(size_t count);
    size_t AcknowledgeCount();
    std::wstring ErrorMessage();

    Commit       At(size_t i);
    std::wstring Sha(size_t i);
    bool         Subject(size_t i, std::wstring& out);
    size_t       IndexOf(std::wstring_view sha);

    void SetUnloadFar(bool on);

private:
    void Run(std::vector<std::wstring> args, std::wstring cwd);
    void Restore(size_t i);
    void Consume(std::string_view bytes);
    void Publish(std::vector<std::string_view> records, bool finished);
    bool ClaimPost();

    template <typename F>
    auto Locked(F&& f) {
        std::lock_guard lock(mu_);
        return f();
    }

    std::mutex                mu_;
    std::condition_variable   cv_;
    ProcessCanceller          canceller_;
    std::string               pending_;
    CommitStore               store_;
    size_t                    acked_    = 0;
    size_t                    wanted_   = kCommitPage;
    bool                      finished_ = false;
    bool                      stop_     = false;
    bool                      posted_   = false;
    std::function<void()>     notify_;
    std::wstring              error_;
    std::wstring              cwd_;
    std::vector<std::wstring> restoreArgs_;
    std::thread               worker_;
};

struct CommitListResult {
    std::unique_ptr<CommitLoader> loader;
    size_t                        count = 0;
    std::wstring                  errorMessage;
};

CommitListResult StartCommitLog(const std::vector<std::wstring>& logArgs,
                                const std::wstring& cwd,
                                size_t first = kCommitPage);

std::wstring CurrentBranchLabel(const std::wstring& cwd);

std::wstring QueryBranchLabel(const std::vector<std::wstring>& logArgs,
                              const std::wstring& cwd);

inline std::vector<std::wstring> RangeLogArgs(const std::wstring& oldSha,
                                              const std::wstring& newSha) {
    return {oldSha + L".." + newSha};
}

CommitDetails LoadCommitDetails(const std::wstring& sha,
                                const std::wstring& cwd,
                                ProcessCanceller* cancel = nullptr);

std::wstring LoadCommitMessage(const std::wstring& sha,
                               const std::wstring& cwd);

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

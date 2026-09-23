#include "git/Git.hpp"

#include "git/Config.hpp"
#include "git/Transcript.hpp"
#include "ui/Encoding.hpp"

#include <iterator>
#include <unordered_map>

namespace git_tools {

ProcessResult RunGit(const std::vector<std::wstring>& args,
                     const std::wstring& cwd,
                     StdioMode stdio,
                     ProcessCanceller* cancel,
                     const OutputSink& onStdout,
                     const std::string* input) {
    NoteGitStart(args);
    ProcessResult r =
        RunProcess(L"git.exe", args, cwd, stdio, cancel, onStdout, input);
    if (cancel && cancel->Cancelled()) RecordGitCancelled(args);
    else                               RecordGitRun(args, r);
    return r;
}

RepoContext OpenRepo() {
    RepoContext repo;
    repo.cwd = CurrentDirectory();

    ProcessResult top = RunGit({L"rev-parse", L"--show-toplevel"}, repo.cwd);
    if (!top.started) {
        repo.errorMessage = L"Failed to launch git:\n\n" + top.errorMessage;
        return repo;
    }
    repo.workTree = TrimmedOutput(top);
    if (!repo.workTree.empty()) {
        repo.root = repo.workTree;
        return repo;
    }

    ProcessResult gitDir =
        RunGit({L"rev-parse", L"--absolute-git-dir"}, repo.cwd);
    repo.root = TrimmedOutput(gitDir);
    if (repo.root.empty()) {
        repo.errorMessage = L"Not inside a git repository:\n\n" +
                            Utf8ToWide(RStrip(gitDir.stderrText));
    }
    return repo;
}

namespace {

constexpr wchar_t kDiffMerges[] = L"--diff-merges=first-parent";
constexpr wchar_t kLogFormat[]  =
    L"--pretty=format:%H%x1f%h%x1f%an%x1f%ae%x1f%ad%x1f%s";
constexpr wchar_t kLogDate[]    = L"--date=format:%Y-%m-%d %H:%M:%S";
constexpr size_t  kResidentPages = 8;

std::wstring GitFailure(const wchar_t* what, const ProcessResult& r) {
    if (!r.started) return r.errorMessage;
    if (r.exitCode == 0) return {};
    return std::wstring(what) + L" failed (exit " +
           std::to_wstring(r.exitCode) + L"):\n" + Utf8ToWide(r.stderrText);
}

std::vector<std::wstring> SplitOn(const std::wstring& s, wchar_t delim) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == delim) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

FileChangeKind KindFromChar(wchar_t c) {
    switch (c) {
        case L'A': return FileChangeKind::Added;
        case L'D': return FileChangeKind::Deleted;
        case L'M': return FileChangeKind::Modified;
        case L'R': return FileChangeKind::Renamed;
        case L'C': return FileChangeKind::Copied;
        case L'T': return FileChangeKind::TypeChanged;
        default:   return FileChangeKind::Other;
    }
}

template <typename F>
void ForEachLine(const std::wstring& text, F&& fn) {
    std::wstring line;
    for (wchar_t ch : text) {
        if (ch == L'\n') { if (!line.empty()) { fn(line); line.clear(); } }
        else if (ch != L'\r') line.push_back(ch);
    }
    if (!line.empty()) fn(line);
}

std::wstring NormalizeNumstatPath(const std::wstring& p) {
    size_t arrow = p.find(L" => ");
    if (arrow == std::wstring::npos) return p;
    size_t braceOpen = p.rfind(L'{', arrow);
    if (braceOpen != std::wstring::npos) {
        size_t braceClose = p.find(L'}', arrow);
        if (braceClose != std::wstring::npos) {
            std::wstring out = p.substr(0, braceOpen);
            out += p.substr(arrow + 4, braceClose - arrow - 4);
            out += p.substr(braceClose + 1);
            return out;
        }
    }
    return p.substr(arrow + 4);
}

struct NumStat {
    int insertions = -1;
    int deletions  = -1;
};

int ParseCount(const std::wstring& field) {
    if (field == L"-") return -2;
    if (field.empty()) return -1;
    long long value = 0;
    for (wchar_t c : field) {
        if (c < L'0' || c > L'9') return -1;
        value = value * 10 + (c - L'0');
        if (value > 1000000000) return 1000000000;
    }
    return static_cast<int>(value);
}

std::unordered_map<std::wstring, NumStat>
ParseNumstat(const std::wstring& output) {
    std::unordered_map<std::wstring, NumStat> result;
    ForEachLine(output, [&](const std::wstring& line) {
        if (line[0] == L':') return;
        std::vector<std::wstring> fields = SplitOn(line, L'\t');
        if (fields.size() >= 3) {
            NumStat ns;
            ns.insertions = ParseCount(fields[0]);
            ns.deletions  = ParseCount(fields[1]);
            result[NormalizeNumstatPath(fields[2])] = ns;
        }
    });
    return result;
}

std::vector<FileChange> ParseRawStatus(const std::wstring& output) {
    std::vector<FileChange> result;
    ForEachLine(output, [&](const std::wstring& line) {
        if (line[0] != L':') return;
        const size_t tab   = line.find(L'\t');
        const size_t space = line.rfind(L' ', tab);
        if (tab == std::wstring::npos || space == std::wstring::npos) return;
        std::vector<std::wstring> fields = SplitOn(line.substr(space + 1), L'\t');
        if (fields.size() >= 2 && !fields[0].empty()) {
            FileChange fc;
            fc.kindChar = fields[0][0];
            fc.kind = KindFromChar(fc.kindChar);
            if ((fc.kind == FileChangeKind::Renamed ||
                 fc.kind == FileChangeKind::Copied) &&
                fields.size() >= 3) {
                fc.oldPath = std::move(fields[1]);
                fc.path    = std::move(fields[2]);
            } else {
                fc.path = std::move(fields[1]);
            }
            result.push_back(std::move(fc));
        }
    });
    return result;
}

std::wstring RStripW(std::wstring s) {
    while (!s.empty() &&
           (s.back() == L'\n' || s.back() == L'\r' ||
            s.back() == L' '  || s.back() == L'\t')) {
        s.pop_back();
    }
    return s;
}

bool StartsWith(const std::wstring& s, const wchar_t* prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool IsRefName(const std::wstring& name, const std::wstring& cwd) {
    ProcessResult r = RunGit(
        {L"rev-parse", L"--symbolic-full-name", L"--verify", L"-q", name}, cwd);
    if (!r.started || r.exitCode != 0) return false;
    return RStrip(r.stdoutText).rfind("refs/", 0) == 0;
}

std::vector<std::wstring> StripFormatArgs(
    const std::vector<std::wstring>& args) {
    std::vector<std::wstring> out;
    out.reserve(args.size());
    bool pathspecs = false;
    for (const auto& a : args) {
        if (!pathspecs) {
            if (a == L"--") {
                pathspecs = true;
            } else if (a == L"--oneline" || a == L"--pretty" ||
                       StartsWith(a, L"--pretty=") ||
                       StartsWith(a, L"--format=")) {
                continue;
            }
        }
        out.push_back(a);
    }
    return out;
}

std::vector<std::wstring> DisplayArgs(const std::vector<std::wstring>& args) {
    std::vector<std::wstring> out;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        if (a == L"--") break;
        if (a == L"--date" && i + 1 < args.size()) {
            out.push_back(a);
            out.push_back(args[++i]);
        } else if (StartsWith(a, L"--date=") || a == L"--relative-date" ||
                   StartsWith(a, L"--encoding=") ||
                   a == L"--mailmap" || a == L"--no-mailmap" ||
                   a == L"--use-mailmap" || a == L"--no-use-mailmap") {
            out.push_back(a);
        }
    }
    return out;
}

}

CommitLoader::CommitLoader(const std::vector<std::wstring>& logArgs,
                           const std::wstring& cwd)
    : cwd_(cwd) {
    std::vector<std::wstring> args{L"log", L"-z", kLogFormat, kLogDate};
    const std::vector<std::wstring> userArgs = StripFormatArgs(logArgs);
    args.insert(args.end(), userArgs.begin(), userArgs.end());

    restoreArgs_ = {L"log", L"--no-walk=unsorted", L"--stdin", L"-z",
                    kLogFormat, kLogDate};
    const std::vector<std::wstring> display = DisplayArgs(logArgs);
    restoreArgs_.insert(restoreArgs_.end(), display.begin(), display.end());

    worker_ = std::thread(&CommitLoader::Run, this, std::move(args), cwd);
}

CommitLoader::~CommitLoader() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    canceller_.Cancel();
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void CommitLoader::Notify(HWND hwnd, UINT message) {
    bool post = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        hwnd_    = hwnd;
        message_ = message;
        post     = store_.size() > acked_ && !posted_;
        if (post) posted_ = true;
    }
    if (post) PostMessageW(hwnd, message, 0, 0);
}

void CommitLoader::Request(size_t count) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (count <= wanted_) return;
        wanted_ = count;
    }
    cv_.notify_all();
}

void CommitLoader::WaitFor(size_t count) {
    Request(count);
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [&] { return finished_ || store_.size() >= count; });
}

size_t CommitLoader::AcknowledgeCount() {
    std::lock_guard<std::mutex> lock(mu_);
    posted_ = false;
    acked_  = store_.size();
    return acked_;
}

std::wstring CommitLoader::ErrorMessage() {
    std::lock_guard<std::mutex> lock(mu_);
    return error_;
}

Commit CommitLoader::At(size_t i) {
    Restore(i);
    std::lock_guard<std::mutex> lock(mu_);
    return store_.At(i);
}

std::wstring CommitLoader::Sha(size_t i) {
    std::lock_guard<std::mutex> lock(mu_);
    return store_.Sha(i);
}

bool CommitLoader::Subject(size_t i, std::wstring& out) {
    std::lock_guard<std::mutex> lock(mu_);
    return store_.Subject(i, out);
}

size_t CommitLoader::IndexOf(std::wstring_view sha) {
    std::lock_guard<std::mutex> lock(mu_);
    return store_.IndexOf(sha);
}

void CommitLoader::SetUnloadFar(bool on) {
    std::lock_guard<std::mutex> lock(mu_);
    store_.SetResidentLimit(on ? kResidentPages : 0);
}

void CommitLoader::Restore(size_t i) {
    std::string revisions;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!store_.NeedsRestore(i)) return;
        revisions = store_.Revisions(i);
    }
    ProcessResult r = RunGit(restoreArgs_, cwd_, StdioMode::Capture, nullptr,
                             {}, &revisions);
    const std::string_view output = (r.started && r.exitCode == 0)
                                        ? std::string_view(r.stdoutText)
                                        : std::string_view();
    std::lock_guard<std::mutex> lock(mu_);
    if (store_.NeedsRestore(i)) store_.Restore(i, output);
}

void CommitLoader::Run(std::vector<std::wstring> args, std::wstring cwd) {
    ProcessResult r = RunGit(args, cwd, StdioMode::Capture, &canceller_,
                             [this](std::string_view bytes) { Consume(bytes); });
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stop_) return;
        error_ = GitFailure(L"git log", r);
    }
    Publish({pending_}, true);
    pending_.clear();
    pending_.shrink_to_fit();
}

void CommitLoader::Consume(std::string_view bytes) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stop_) return;
    }

    pending_.append(bytes.data(), bytes.size());
    std::vector<std::string_view> records;
    const std::string_view buffered = pending_;
    size_t start = 0;
    for (size_t nul = buffered.find('\0'); nul != std::string_view::npos;
         nul = buffered.find('\0', start)) {
        records.push_back(buffered.substr(start, nul - start));
        start = nul + 1;
    }
    Publish(std::move(records), false);
    pending_.erase(0, start);

    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return stop_ || store_.size() < wanted_; });
}

void CommitLoader::Publish(std::vector<std::string_view> records,
                           bool finished) {
    HWND target  = nullptr;
    UINT message = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (records.empty() && !finished) return;
        for (std::string_view record : records) store_.Append(record);
        if (finished) finished_ = true;
        if (hwnd_ && !posted_ && store_.size() > acked_) {
            posted_ = true;
            target  = hwnd_;
            message = message_;
        }
    }
    cv_.notify_all();
    if (target) PostMessageW(target, message, 0, 0);
}

CommitListResult StartCommitLog(const std::vector<std::wstring>& logArgs,
                                const std::wstring& cwd,
                                size_t first) {
    CommitListResult result;
    result.loader = std::make_unique<CommitLoader>(logArgs, cwd);
    result.loader->SetUnloadFar(ConfigGetBool(kUnloadFarCommitsKey, false));
    result.loader->WaitFor(first);
    result.count        = result.loader->AcknowledgeCount();
    result.errorMessage = result.loader->ErrorMessage();
    return result;
}

std::wstring CurrentBranchLabel(const std::wstring& cwd) {
    std::wstring branch =
        TrimmedOutput(RunGit({L"symbolic-ref", L"--short", L"-q", L"HEAD"}, cwd));
    if (!branch.empty()) return branch;

    std::wstring sha =
        TrimmedOutput(RunGit({L"rev-parse", L"--short", L"HEAD"}, cwd));
    return sha.empty() ? std::wstring() : L"detached at " + sha;
}

std::wstring QueryBranchLabel(const std::vector<std::wstring>& logArgs,
                              const std::wstring& cwd) {
    std::vector<std::wstring> refs;
    for (const auto& a : logArgs) {
        if (a == L"--") break;
        if (a.empty() || a[0] == L'-') continue;
        if (IsRefName(a, cwd)) refs.push_back(a);
    }
    if (refs.size() == 1) return refs.front();
    return CurrentBranchLabel(cwd);
}

std::wstring LoadFilesDiff(const std::wstring& sha,
                           const std::vector<std::wstring>& paths,
                           const std::wstring& cwd) {
    if (paths.empty()) return {};
    std::vector<std::wstring> args{
        L"show", L"--format=", L"--no-color", kDiffMerges, sha, L"--",
    };
    args.insert(args.end(), paths.begin(), paths.end());
    ProcessResult r = RunGit(args, cwd);
    if (!r.started || r.exitCode != 0) return {};
    return Utf8ToWide(r.stdoutText);
}

BranchListResult LoadBranchList(const std::wstring& cwd) {
    BranchListResult result;
    ProcessResult r = RunGit({
        L"for-each-ref",
        L"--format=%(HEAD)\x1f%(refname)\x1f%(refname:short)\x1f"
        L"%(upstream:short)\x1f%(objectname:short)\x1f%(subject)",
        L"refs/heads",
        L"refs/remotes",
    }, cwd);
    result.errorMessage = GitFailure(L"git for-each-ref", r);
    if (!result.errorMessage.empty()) return result;
    ForEachLine(Utf8ToWide(r.stdoutText), [&](const std::wstring& line) {
        std::vector<std::wstring> fields = SplitOn(line, L'\x1f');
        if (fields.size() == 6) {
            Branch b;
            b.isCurrent = (!fields[0].empty() && fields[0][0] == L'*');
            const std::wstring& fullRef = fields[1];
            b.isRemote  = (fullRef.compare(0, 13, L"refs/remotes/") == 0);
            b.name      = std::move(fields[2]);
            b.upstream  = std::move(fields[3]);
            b.shortSha  = std::move(fields[4]);
            b.subject   = std::move(fields[5]);
            const bool isHead =
                (b.name == L"HEAD") ||
                (b.name.size() >= 5 &&
                 b.name.compare(b.name.size() - 5, 5, L"/HEAD") == 0);
            if (!isHead) result.branches.push_back(std::move(b));
        }
    });
    return result;
}

ProcessResult CheckoutBranch(const std::wstring& name,
                             const std::wstring& cwd) {
    return RunGit({L"checkout", name}, cwd);
}

CommitDetails LoadCommitDetails(const std::wstring& sha,
                                const std::wstring& cwd,
                                ProcessCanceller* cancel) {
    CommitDetails details;
    details.sha = sha;

    ProcessResult r = RunGit(
        {L"show", L"--format=%B%x00", kDiffMerges, L"--raw", L"--numstat", sha},
        cwd, StdioMode::Capture, cancel);
    if (!r.started || r.exitCode != 0) return details;

    const std::wstring output = Utf8ToWide(r.stdoutText);
    const size_t nul = output.find(L'\0');
    std::wstring diff;
    if (nul != std::wstring::npos) {
        details.message = RStripW(output.substr(0, nul));
        diff = output.substr(nul + 1);
    } else {
        diff = output;
    }

    details.changes = ParseRawStatus(diff);
    auto stats = ParseNumstat(diff);
    for (auto& fc : details.changes) {
        auto it = stats.find(fc.path);
        if (it != stats.end()) {
            fc.insertions = it->second.insertions;
            fc.deletions  = it->second.deletions;
        }
    }
    return details;
}

std::wstring LoadCommitMessage(const std::wstring& sha,
                               const std::wstring& cwd) {
    ProcessResult r = RunGit({L"show", L"-s", L"--format=%B", sha}, cwd);
    if (!r.started || r.exitCode != 0) return {};
    return RStripW(Utf8ToWide(r.stdoutText));
}

}

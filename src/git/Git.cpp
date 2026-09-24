#include "git/Git.hpp"

#include "git/Config.hpp"
#include "git/Transcript.hpp"
#include "util/System.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace git_tools {

namespace {

#ifdef _WIN32
constexpr wchar_t kGitExecutable[] = L"git.exe";
#else
constexpr wchar_t kGitExecutable[] = L"git";
#endif

}

ProcessResult RunGit(const std::vector<std::wstring>& args,
                     const std::wstring& cwd,
                     StdioMode stdio,
                     ProcessCanceller* cancel,
                     const OutputSink& onStdout,
                     const std::string* input) {
    NoteGitStart(args);
    ProcessResult r =
        RunProcess(kGitExecutable, args, cwd, stdio, cancel, onStdout, input);
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
                            Utf8ToWide(TrimRight(gitDir.stderrText));
    }
    return repo;
}

namespace {

constexpr wchar_t kDiffMerges[] = L"--diff-merges=first-parent";
constexpr wchar_t kLogFormat[]  =
    L"--pretty=format:%H%x1f%h%x1f%an%x1f%ae%x1f%ad%x1f%s";
constexpr wchar_t kLogDate[]    = L"--date=format:%Y-%m-%d %H:%M:%S";
constexpr size_t  kResidentPages = 8;

constexpr std::wstring_view kFormatFlags[]   = {L"--oneline", L"--pretty"};
constexpr std::wstring_view kFormatPrefix[]  = {L"--pretty=", L"--format="};
constexpr std::wstring_view kDisplayFlags[]  = {
    L"--relative-date", L"--mailmap",        L"--no-mailmap",
    L"--use-mailmap",   L"--no-use-mailmap",
};
constexpr std::wstring_view kDisplayPrefix[] = {L"--date=", L"--encoding="};

template <size_t N>
bool IsOneOf(std::wstring_view arg, const std::wstring_view (&flags)[N]) {
    return std::ranges::find(flags, arg) != std::end(flags);
}

template <size_t N>
bool HasPrefix(std::wstring_view arg, const std::wstring_view (&prefixes)[N]) {
    return std::ranges::any_of(prefixes, [arg](std::wstring_view p) {
        return arg.starts_with(p);
    });
}

std::wstring GitFailure(const wchar_t* what, const ProcessResult& r) {
    if (!r.started) return r.errorMessage;
    if (r.exitCode == 0) return {};
    return std::wstring(what) + L" failed (exit " +
           std::to_wstring(r.exitCode) + L"):\n" + Utf8ToWide(r.stderrText);
}

std::vector<std::wstring> Concat(std::vector<std::wstring> head,
                                 const std::vector<std::wstring>& tail) {
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
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

std::wstring NormalizeNumstatPath(std::wstring_view p) {
    const size_t arrow = p.find(L" => ");
    if (arrow == p.npos) return std::wstring(p);
    const size_t open  = p.rfind(L'{', arrow);
    const size_t close = p.find(L'}', arrow);
    if (open == p.npos || close == p.npos) {
        return std::wstring(p.substr(arrow + 4));
    }
    std::wstring out(p.substr(0, open));
    out += p.substr(arrow + 4, close - arrow - 4);
    out += p.substr(close + 1);
    return out;
}

struct NumStat {
    int insertions = -1;
    int deletions  = -1;
};

int ParseCount(std::wstring_view field) {
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

std::unordered_map<std::wstring, NumStat> ParseNumstat(std::wstring_view diff) {
    std::unordered_map<std::wstring, NumStat> result;
    ForEachLine(diff, [&](std::wstring_view line) {
        if (line.empty() || line[0] == L':') return;
        const std::vector<std::wstring> fields = Split(line, L'\t');
        if (fields.size() < 3) return;
        result[NormalizeNumstatPath(fields[2])] =
            NumStat{ParseCount(fields[0]), ParseCount(fields[1])};
    });
    return result;
}

std::vector<FileChange> ParseRawStatus(std::wstring_view diff) {
    std::vector<FileChange> result;
    ForEachLine(diff, [&](std::wstring_view line) {
        if (line.empty() || line[0] != L':') return;
        const size_t tab   = line.find(L'\t');
        const size_t space = line.rfind(L' ', tab);
        if (tab == line.npos || space == line.npos) return;
        std::vector<std::wstring> fields = Split(line.substr(space + 1), L'\t');
        if (fields.size() < 2 || fields[0].empty()) return;
        FileChange& fc = result.emplace_back();
        fc.kindChar = fields[0][0];
        fc.kind     = KindFromChar(fc.kindChar);
        const bool twoPaths = (fc.kind == FileChangeKind::Renamed ||
                               fc.kind == FileChangeKind::Copied) &&
                              fields.size() >= 3;
        if (twoPaths) fc.oldPath = std::move(fields[1]);
        fc.path = std::move(fields[twoPaths ? 2 : 1]);
    });
    return result;
}

bool IsRefName(const std::wstring& name, const std::wstring& cwd) {
    ProcessResult r = RunGit(
        {L"rev-parse", L"--symbolic-full-name", L"--verify", L"-q", name}, cwd);
    return r.ok() && TrimRight(r.stdoutText).starts_with("refs/");
}

std::vector<std::wstring> StripFormatArgs(const std::vector<std::wstring>& args) {
    std::vector<std::wstring> out;
    bool pathspecs = false;
    for (const std::wstring& a : args) {
        pathspecs = pathspecs || a == L"--";
        if (pathspecs || !(IsOneOf(a, kFormatFlags) || HasPrefix(a, kFormatPrefix))) {
            out.push_back(a);
        }
    }
    return out;
}

std::vector<std::wstring> DisplayArgs(const std::vector<std::wstring>& args) {
    std::vector<std::wstring> out;
    for (size_t i = 0; i < args.size() && args[i] != L"--"; ++i) {
        if (args[i] == L"--date" && i + 1 < args.size()) {
            out.push_back(args[i]);
            out.push_back(args[++i]);
        } else if (IsOneOf(args[i], kDisplayFlags) ||
                   HasPrefix(args[i], kDisplayPrefix)) {
            out.push_back(args[i]);
        }
    }
    return out;
}

}

CommitLoader::CommitLoader(const std::vector<std::wstring>& logArgs,
                           const std::wstring& cwd)
    : cwd_(cwd),
      restoreArgs_(Concat({L"log", L"--no-walk=unsorted", L"--stdin", L"-z",
                           kLogFormat, kLogDate},
                          DisplayArgs(logArgs))) {
    worker_ = std::thread(&CommitLoader::Run, this,
                          Concat({L"log", L"-z", kLogFormat, kLogDate},
                                 StripFormatArgs(logArgs)),
                          cwd);
}

CommitLoader::~CommitLoader() {
    Locked([this] { stop_ = true; });
    canceller_.Cancel();
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool CommitLoader::ClaimPost() {
    if (!notify_ || posted_ || store_.size() <= acked_) return false;
    posted_ = true;
    return true;
}

void CommitLoader::Notify(std::function<void()> onCommits) {
    std::lock_guard lock(mu_);
    notify_ = std::move(onCommits);
    if (ClaimPost()) notify_();
}

void CommitLoader::Request(size_t count) {
    const bool raised = Locked([&] {
        if (count <= wanted_) return false;
        wanted_ = count;
        return true;
    });
    if (raised) cv_.notify_all();
}

void CommitLoader::WaitFor(size_t count) {
    Request(count);
    std::unique_lock lock(mu_);
    cv_.wait(lock, [&] { return finished_ || store_.size() >= count; });
}

size_t CommitLoader::AcknowledgeCount() {
    return Locked([this] {
        posted_ = false;
        acked_  = store_.size();
        return acked_;
    });
}

std::wstring CommitLoader::ErrorMessage() {
    return Locked([this] { return error_; });
}

Commit CommitLoader::At(size_t i) {
    Restore(i);
    return Locked([&] { return store_.At(i); });
}

std::wstring CommitLoader::Sha(size_t i) {
    return Locked([&] { return store_.Sha(i); });
}

bool CommitLoader::Subject(size_t i, std::wstring& out) {
    return Locked([&] { return store_.Subject(i, out); });
}

size_t CommitLoader::IndexOf(std::wstring_view sha) {
    return Locked([&] { return store_.IndexOf(sha); });
}

void CommitLoader::SetUnloadFar(bool on) {
    Locked([&] { store_.SetResidentLimit(on ? kResidentPages : 0); });
}

void CommitLoader::Restore(size_t i) {
    std::string revisions;
    const bool needed = Locked([&] {
        if (!store_.NeedsRestore(i)) return false;
        revisions = store_.Revisions(i);
        return true;
    });
    if (!needed) return;

    const ProcessResult r = RunGit(restoreArgs_, cwd_, StdioMode::Capture,
                                   nullptr, {}, &revisions);
    const std::string_view output =
        r.ok() ? std::string_view(r.stdoutText) : std::string_view();
    Locked([&] {
        if (store_.NeedsRestore(i)) store_.Restore(i, output);
    });
}

void CommitLoader::Run(std::vector<std::wstring> args, std::wstring cwd) {
    ProcessResult r = RunGit(args, cwd, StdioMode::Capture, &canceller_,
                             [this](std::string_view bytes) { Consume(bytes); });
    const bool stopped = Locked([&] {
        if (!stop_) error_ = GitFailure(L"git log", r);
        return stop_;
    });
    if (stopped) return;
    Publish({pending_}, true);
    std::string().swap(pending_);
}

void CommitLoader::Consume(std::string_view bytes) {
    if (Locked([this] { return stop_; })) return;

    pending_.append(bytes);
    std::vector<std::string_view> records;
    const std::string_view buffered = pending_;
    size_t start = 0;
    for (size_t nul = buffered.find('\0'); nul != buffered.npos;
         nul = buffered.find('\0', start)) {
        records.push_back(buffered.substr(start, nul - start));
        start = nul + 1;
    }
    Publish(std::move(records), false);
    pending_.erase(0, start);

    std::unique_lock lock(mu_);
    cv_.wait(lock, [this] { return stop_ || store_.size() < wanted_; });
}

void CommitLoader::Publish(std::vector<std::string_view> records,
                           bool finished) {
    {
        std::lock_guard lock(mu_);
        if (records.empty() && !finished) return;
        for (std::string_view record : records) store_.Append(record);
        finished_ = finished_ || finished;
        if (ClaimPost()) notify_();
    }
    cv_.notify_all();
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
    for (const std::wstring& a : logArgs) {
        if (a == L"--") break;
        if (!a.empty() && a[0] != L'-' && IsRefName(a, cwd)) refs.push_back(a);
    }
    return refs.size() == 1 ? refs.front() : CurrentBranchLabel(cwd);
}

std::wstring LoadFilesDiff(const std::wstring& sha,
                           const std::vector<std::wstring>& paths,
                           const std::wstring& cwd) {
    if (paths.empty()) return {};
    ProcessResult r = RunGit(
        Concat({L"show", L"--format=", L"--no-color", kDiffMerges, sha, L"--"},
               paths),
        cwd);
    return r.ok() ? Utf8ToWide(r.stdoutText) : std::wstring();
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
    ForEachLine(Utf8ToWide(r.stdoutText), [&](std::wstring_view line) {
        std::vector<std::wstring> fields = Split(line, L'\x1f');
        if (fields.size() != 6) return;
        Branch b;
        b.isCurrent = fields[0].starts_with(L'*');
        b.isRemote  = fields[1].starts_with(L"refs/remotes/");
        b.name      = std::move(fields[2]);
        b.upstream  = std::move(fields[3]);
        b.shortSha  = std::move(fields[4]);
        b.subject   = std::move(fields[5]);
        if (b.name != L"HEAD" && !b.name.ends_with(L"/HEAD")) {
            result.branches.push_back(std::move(b));
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
    if (!r.ok()) return details;

    const std::wstring      output = Utf8ToWide(r.stdoutText);
    const std::wstring_view view   = output;
    const size_t            nul    = view.find(L'\0');
    const std::wstring_view diff   =
        nul == view.npos ? view : view.substr(nul + 1);
    if (nul != view.npos) details.message = TrimRight(view.substr(0, nul));

    details.changes = ParseRawStatus(diff);
    const auto stats = ParseNumstat(diff);
    for (FileChange& fc : details.changes) {
        if (auto it = stats.find(fc.path); it != stats.end()) {
            fc.insertions = it->second.insertions;
            fc.deletions  = it->second.deletions;
        }
    }
    return details;
}

std::wstring LoadCommitMessage(const std::wstring& sha,
                               const std::wstring& cwd) {
    ProcessResult r = RunGit({L"show", L"-s", L"--format=%B", sha}, cwd);
    return r.ok() ? TrimRight(Utf8ToWide(r.stdoutText)) : std::wstring();
}

}

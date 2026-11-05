#include "git/Git.hpp"

#include "git/Config.hpp"
#include "git/Transcript.hpp"
#include "util/System.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

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

std::wstring GitFailure(const std::wstring& what, const ProcessResult& r) {
    if (!r.started) return L"Failed to launch git:\n\n" + r.errorMessage;
    if (r.exitCode == 0) return {};
    return what + L" failed (exit " + std::to_wstring(r.exitCode) + L"):\n" +
           Utf8ToWide(TrimRight(r.stderrText));
}

RepoContext OpenRepo() {
    RepoContext repo;
    repo.cwd = CurrentDirectory();

    ProcessResult top = RunGit({L"rev-parse", L"--show-toplevel"}, repo.cwd);
    if (!top.started) {
        repo.errorMessage = GitFailure(L"git rev-parse", top);
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
// Each commit starts with a line holding just this byte; its shortstat line follows.
constexpr wchar_t kStatFormat[] = L"--format=%x01";
constexpr char    kStatMarker   = '\x01';
// A row this far past the counted ones is a jump, not a step; waiting would freeze.
constexpr size_t  kStatWaitRows = 100;

constexpr std::wstring_view kFormatFlags[]   = {L"--oneline", L"--pretty"};
constexpr std::wstring_view kFormatPrefix[]  = {L"--pretty=", L"--format="};
constexpr std::wstring_view kDisplayFlags[]  = {
    L"--relative-date", L"--mailmap",        L"--no-mailmap",
    L"--use-mailmap",   L"--no-use-mailmap",
};
constexpr std::wstring_view kDisplayPrefix[] = {L"--date=", L"--encoding="};

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
    long long value = 0;
    return ParseDigits(field, value) ? static_cast<int>(std::min(value, 1000000000LL))
                                     : -1;
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
        const bool twoPaths = HasOldPath(fc.kind) && fields.size() >= 3;
        if (twoPaths) fc.oldPath = std::move(fields[1]);
        fc.path = std::move(fields[twoPaths ? 2 : 1]);
    });
    return result;
}

std::wstring CurrentBranchLabel(const std::wstring& cwd) {
    std::wstring branch = GitOutput({L"symbolic-ref", L"--short", L"-q", L"HEAD"}, cwd);
    if (!branch.empty()) return branch;

    std::wstring sha = GitOutput({L"rev-parse", L"--short", L"HEAD"}, cwd);
    return sha.empty() ? std::wstring() : L"detached at " + sha;
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
        if (pathspecs || !(IsOneOf(a, kFormatFlags) || StartsWithAny(a, kFormatPrefix))) {
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
                   StartsWithAny(args[i], kDisplayPrefix)) {
            out.push_back(args[i]);
        }
    }
    return out;
}

// " 2 files changed, 39 insertions(+), 15 deletions(-)": git omits a zero count.
CommitStat ParseShortStat(std::string_view line) {
    CommitStat stat;
    while (!line.empty()) {
        const size_t comma = std::min(line.find(','), line.size());
        std::string_view part = line.substr(0, comma);
        line.remove_prefix(std::min(comma + 1, line.size()));

        part.remove_prefix(std::min(part.find_first_not_of(' '), part.size()));
        long long value = 0;
        if (!ParseDigits(part.substr(0, part.find(' ')), value)) continue;
        const int count = static_cast<int>(std::min(value, 1000000000LL));
        if (part.ends_with("(+)")) stat.insertions = count;
        if (part.ends_with("(-)")) stat.deletions = count;
    }
    return stat;
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

CommitStatsLoader::CommitStatsLoader(const std::vector<std::wstring>& logArgs,
                                     const std::wstring& cwd) {
    // --full-diff keeps a pathspec from narrowing the counts below the commit's details.
    worker_ = std::thread(&CommitStatsLoader::Run, this,
                          Concat({L"log", kStatFormat, L"--shortstat", kDiffMerges,
                                  L"--full-diff"},
                                 StripFormatArgs(logArgs)),
                          cwd);
}

CommitStatsLoader::~CommitStatsLoader() {
    {
        std::lock_guard lock(mu_);
        stop_ = true;
    }
    canceller_.Cancel();
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void CommitStatsLoader::Notify(std::function<void()> onStats) {
    std::lock_guard lock(mu_);
    notify_ = std::move(onStats);
}

void CommitStatsLoader::Acknowledge() {
    std::lock_guard lock(mu_);
    posted_ = false;
}

void CommitStatsLoader::Request(size_t count) {
    {
        std::lock_guard lock(mu_);
        if (count <= wanted_) return;
        wanted_ = count;
    }
    cv_.notify_all();
}

std::optional<CommitStat> CommitStatsLoader::At(size_t i, bool wait) {
    std::unique_lock lock(mu_);
    if (wait && !finished_ && i >= counted_ && i < counted_ + kStatWaitRows) {
        wanted_ = std::max(wanted_, i + 1);
        cv_.notify_all();
        cv_.wait(lock, [&] { return stop_ || finished_ || i < counted_; });
    }
    if (i >= counted_) return std::nullopt;
    return stats_[i];
}

void CommitStatsLoader::Run(std::vector<std::wstring> args, std::wstring cwd) {
    RunGit(args, cwd, StdioMode::Capture, &canceller_,
           [this](std::string_view bytes) { Consume(bytes); });
    {
        std::lock_guard lock(mu_);
        if (stop_) return;
        ParseLine(pending_);
        finished_ = true;
        counted_  = stats_.size();
        Post();
    }
    cv_.notify_all();
}

void CommitStatsLoader::Consume(std::string_view bytes) {
    pending_.append(bytes);
    std::unique_lock lock(mu_);
    if (stop_) return;

    const size_t before = counted_;
    const std::string_view buffered = pending_;
    size_t start = 0;
    for (size_t eol = buffered.find('\n'); eol != buffered.npos;
         eol = buffered.find('\n', start)) {
        ParseLine(buffered.substr(start, eol - start));
        start = eol + 1;
    }
    pending_.erase(0, start);

    if (counted_ != before) {
        cv_.notify_all();
        Post();
    }
    cv_.wait(lock, [this] { return stop_ || counted_ < wanted_; });
}

void CommitStatsLoader::Post() {
    if (!notify_ || posted_) return;
    posted_ = true;
    notify_();
}

void CommitStatsLoader::ParseLine(std::string_view line) {
    if (line.size() == 1 && line[0] == kStatMarker) {
        counted_ = stats_.size();
        stats_.emplace_back();
    } else if (line.starts_with(' ') && !stats_.empty()) {
        stats_.back() = ParseShortStat(line);
        counted_      = stats_.size();
    }
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

std::vector<std::wstring> WithDateRange(std::vector<std::wstring> logArgs,
                                        const DateRange& range) {
    std::vector<std::wstring> dates;
    if (!range.since.empty()) dates.push_back(L"--since=" + range.since + L" 00:00:00");
    if (!range.until.empty()) dates.push_back(L"--until=" + range.until + L" 23:59:59");
    logArgs.insert(std::ranges::find(logArgs, L"--"), dates.begin(), dates.end());
    return logArgs;
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
        L"%(upstream:short)\x1f%(subject)",
        L"refs/heads",
        L"refs/remotes",
    }, cwd);
    result.errorMessage = GitFailure(L"git for-each-ref", r);
    if (!result.errorMessage.empty()) return result;
    ForEachLine(Utf8ToWide(r.stdoutText), [&](std::wstring_view line) {
        std::vector<std::wstring> fields = Split(line, L'\x1f');
        if (fields.size() != 5) return;
        Branch b;
        b.isCurrent = fields[0].starts_with(L'*');
        b.isRemote  = fields[1].starts_with(L"refs/remotes/");
        b.name      = std::move(fields[2]);
        b.upstream  = std::move(fields[3]);
        b.subject   = std::move(fields[4]);
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

CommitDetailsLoader::CommitDetailsLoader(std::wstring cwd, Done done)
    : cwd_(std::move(cwd)), done_(std::move(done)) {
    worker_ = std::thread(&CommitDetailsLoader::Run, this);
}

CommitDetailsLoader::~CommitDetailsLoader() {
    Stop();
}

void CommitDetailsLoader::Invalidate() {
    ++token_;
    canceller_.Cancel();
}

void CommitDetailsLoader::Load(const std::wstring& sha) {
    {
        std::lock_guard lock(mu_);
        pendingTok_ = token_.load();
        pendingSha_ = sha;
    }
    cv_.notify_one();
}

void CommitDetailsLoader::Stop() {
    {
        std::lock_guard lock(mu_);
        stop_ = true;
    }
    canceller_.Cancel();
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void CommitDetailsLoader::Run() {
    for (;;) {
        int          token;
        std::wstring sha;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, [this] { return stop_ || pendingTok_ >= 0; });
            if (stop_) return;
            token = std::exchange(pendingTok_, -1);
            sha   = pendingSha_;
        }

        canceller_.Reset();
        auto details = std::make_shared<CommitDetails>(
            LoadCommitDetails(sha, cwd_, &canceller_));
        std::lock_guard lock(mu_);
        if (stop_) return;
        done_(token, std::move(details));
    }
}

std::wstring LoadCommitMessage(const std::wstring& sha,
                               const std::wstring& cwd) {
    return GitOutput({L"show", L"-s", L"--format=%B", sha}, cwd);
}

}

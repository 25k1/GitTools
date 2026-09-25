#include "ui/LogWindow.hpp"

#include "git/Config.hpp"
#include "git/Git.hpp"
#include "ui/Shell.hpp"

#include "ui/App.hpp"
#include "ui/Columns.hpp"
#include "ui/DiffWindow.hpp"
#include "ui/ListView.hpp"
#include "ui/ToolFrame.hpp"
#include "ui/Widgets.hpp"

#include <wx/panel.h>
#include <wx/timer.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace git_tools {

namespace {

constexpr int kLoadDelayMs = 250;

constexpr int kCmdOpenLocation = wxID_HIGHEST + 101;
constexpr int kCmdEditFile     = wxID_HIGHEST + 102;
constexpr int kCmdCopyHash     = wxID_HIGHEST + 111;
constexpr int kCmdCopyMessage  = wxID_HIGHEST + 112;
constexpr int kCmdCopyAuthor   = wxID_HIGHEST + 113;
constexpr int kCmdCopyEmail    = wxID_HIGHEST + 114;

bool QueryMentions(const std::wstring& query, const std::wstring& name) {
    if (name.empty()) return false;
    for (size_t pos = query.find(name); pos != std::wstring::npos;
         pos = query.find(name, pos + name.size())) {
        const size_t end = pos + name.size();
        if ((pos == 0 || query[pos - 1] == L' ') &&
            (end == query.size() || query[end] == L' ')) {
            return true;
        }
    }
    return false;
}

std::wstring ComposeTitle(const LogWindowParams& p, const std::wstring& branch) {
    std::wstring t = L"gittools";
    if (!p.query.empty()) t += L" - " + p.query;
    if (!branch.empty() && !QueryMentions(p.query, branch)) t += L" - " + branch;
    if (!p.repoRoot.empty()) t += L" - " + p.repoRoot;
    return t;
}

std::wstring FormatCount(int value, bool suppressed) {
    if (suppressed)  return L"";
    if (value == -2) return L"bin";
    return value < 0 ? std::wstring() : std::to_wstring(value);
}

std::wstring ChangeName(const FileChange& fc) {
    const bool twoPaths = fc.kind == FileChangeKind::Renamed ||
                          fc.kind == FileChangeKind::Copied;
    return (twoPaths && !fc.oldPath.empty()) ? fc.oldPath + L" -> " + fc.path
                                             : fc.path;
}

std::wstring ChangeCell(const FileChange& fc, long column) {
    switch (column) {
        case kChangeName:  return ChangeName(fc);
        case kChangeState: return std::wstring(1, fc.kindChar);
        case kChangeInsertions:
            return FormatCount(fc.insertions, fc.kind == FileChangeKind::Deleted);
        case kChangeDeletions:
            return FormatCount(fc.deletions, fc.kind == FileChangeKind::Added);
        default: return {};
    }
}

class LogFrame : public ToolFrame {
public:
    explicit LogFrame(LogWindowParams params);
    ~LogFrame() override;

protected:
    std::wstring StatusText() const override;
    void         OnOptionsChanged() override;

private:
    size_t CommitCount() const { return shown_; }

    long SelectedIndex() const {
        return RowWithin(commits_->SelectedRow(), CommitCount());
    }

    std::optional<Commit>          SelectedCommit() const;
    std::wstring                   SelectedSha() const;
    const FileChange*              SelectedChange() const;
    std::vector<const FileChange*> SelectedChanges() const;

    std::wstring CommitCell(long row, long column) const;
    std::wstring KnownMessage(const Commit& c) const;
    std::wstring FullMessage(const Commit& c) const;
    const std::wstring* LoadedMessage(const Commit& c) const;

    void ShowCommitMessage();
    void ApplyCommitDetails(CommitDetails&& details);
    void ClearDetailPanes();

    int  BeginDetailLoad();
    void DispatchCommitLoad(const std::wstring& sha);
    void ScheduleCommitLoad();
    void ReloadSelectedCommit();
    void OnDetailLoaded(int token, const std::shared_ptr<CommitDetails>& details);

    void AttachLoader();
    void AppendLoadedCommits();
    void RequestCommitsNear(long row);
    void ReloadCommitList();

    void WorkerLoop();
    void StopDetailWorker();

    void OpenDiffForSelection();
    void CopySelection(bool commits);
    void OnListKey(bool commits, wxKeyEvent& event);
    void ShowCommitsContextMenu(const wxPoint& at);
    void ShowChangesContextMenu(const wxPoint& at);

    LogWindowParams params_;
    std::wstring    branch_;
    VirtualList*    commits_ = nullptr;
    wxTextCtrl*     message_ = nullptr;
    VirtualList*    changes_ = nullptr;
    std::wstring    messageShown_;
    size_t          shown_      = 0;
    CommitDetails   detail_;
    long long       insertions_ = 0;
    long long       deletions_  = 0;
    wxTimer         loadTimer_;

    bool                    loadPending_ = false;
    ProcessCanceller        canceller_;
    std::thread             worker_;
    std::mutex              mu_;
    std::condition_variable cv_;
    bool                    stop_       = false;
    int                     pendingTok_ = -1;
    std::wstring            pendingSha_;
    std::atomic<int>        nextToken_{0};
};

LogFrame::LogFrame(LogWindowParams params)
    : ToolFrame(L"", wxSize(1100, 750)), params_(std::move(params)) {
    branch_ = QueryBranchLabel(params_.logArgs, params_.cwd);
    SetTitle(ComposeTitle(params_, branch_));

    wxPanel* panel = Panel();
    AddLabel(L"&Commits");
    commits_ = new VirtualList(panel, false, kCommitColumns,
                               [this](long row, long column) {
        return CommitCell(row, column);
    });
    AddPane(commits_, 36);
    AddLabel(L"&Message");
    message_ = CreateReadOnlyText(panel);
    AddPane(message_, 18);
    AddLabel(L"C&hanges");
    changes_ = new VirtualList(panel, true, kChangeColumns,
                               [this](long row, long column) {
        return static_cast<size_t>(row) < detail_.changes.size()
                   ? ChangeCell(detail_.changes[static_cast<size_t>(row)], column)
                   : std::wstring();
    });
    AddPane(changes_, 26);
    FinishLayout(changes_, 20);

    commits_->WhenSelected([this] {
        if (const long row = SelectedIndex(); row >= 0) {
            RequestCommitsNear(row);
            ScheduleCommitLoad();
        }
    });
    commits_->WhenRowsNeeded([this](long lastRow) { RequestCommitsNear(lastRow); });
    commits_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) { OnListKey(true, event); });
    changes_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) { OnListKey(false, event); });
    changes_->WhenActivated([this] { OpenDiffForSelection(); });
    commits_->WhenContextMenu([this](const wxPoint& at) { ShowCommitsContextMenu(at); });
    changes_->WhenContextMenu([this](const wxPoint& at) { ShowChangesContextMenu(at); });
    loadTimer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { ReloadSelectedCommit(); });

    if (params_.loader) AttachLoader();
    commits_->ResetRows(CommitCount());
    worker_ = std::thread(&LogFrame::WorkerLoop, this);
    if (CommitCount() > 0) commits_->SelectOnly(0);
    commits_->SetFocus();
}

LogFrame::~LogFrame() {
    loadTimer_.Stop();
    params_.loader.reset();
    StopDetailWorker();
}

std::wstring LogFrame::StatusText() const {
    return loadPending_ ? std::wstring(L"git show - running")
                        : ToolFrame::StatusText();
}

void LogFrame::OnOptionsChanged() {
    commits_->ApplyColumnLayout();
    changes_->ApplyColumnLayout();
    if (params_.loader) {
        params_.loader->SetUnloadFar(ConfigGetBool(kUnloadFarCommitsKey, false));
    }
}

std::optional<Commit> LogFrame::SelectedCommit() const {
    const long i = SelectedIndex();
    if (i < 0) return std::nullopt;
    return params_.loader->At(static_cast<size_t>(i));
}

std::wstring LogFrame::SelectedSha() const {
    const long i = SelectedIndex();
    return (i < 0) ? std::wstring() : params_.loader->Sha(static_cast<size_t>(i));
}

const FileChange* LogFrame::SelectedChange() const {
    const long i = RowWithin(changes_->SelectedRow(), detail_.changes.size());
    return (i < 0) ? nullptr : &detail_.changes[static_cast<size_t>(i)];
}

std::vector<const FileChange*> LogFrame::SelectedChanges() const {
    std::vector<const FileChange*> out;
    for (long i : changes_->SelectedRows()) {
        if (static_cast<size_t>(i) < detail_.changes.size()) {
            out.push_back(&detail_.changes[static_cast<size_t>(i)]);
        }
    }
    return out;
}

std::wstring LogFrame::CommitCell(long row, long column) const {
    if (row < 0 || static_cast<size_t>(row) >= CommitCount() || !params_.loader) {
        return {};
    }
    Commit c = params_.loader->At(static_cast<size_t>(row));
    switch (column) {
        case kCommitSubject: return std::move(c.subject);
        case kCommitAuthor:  return std::move(c.author);
        case kCommitDate:    return std::move(c.date);
        case kCommitInsertions:
        case kCommitDeletions:
            if (c.fullSha != detail_.sha || row != SelectedIndex()) return {};
            return std::to_wstring(column == kCommitInsertions ? insertions_
                                                               : deletions_);
        default: return {};
    }
}

const std::wstring* LogFrame::LoadedMessage(const Commit& c) const {
    return (c.fullSha == detail_.sha && !detail_.message.empty())
               ? &detail_.message
               : nullptr;
}

std::wstring LogFrame::KnownMessage(const Commit& c) const {
    const std::wstring* loaded = LoadedMessage(c);
    return loaded ? *loaded : c.subject;
}

std::wstring LogFrame::FullMessage(const Commit& c) const {
    if (const std::wstring* loaded = LoadedMessage(c)) return *loaded;
    std::wstring message = LoadCommitMessage(c.fullSha, params_.repoRoot);
    return message.empty() ? c.subject : message;
}

void LogFrame::ShowCommitMessage() {
    const std::optional<Commit> c = SelectedCommit();
    std::wstring text =
        c ? c->fullSha + L"\n\n" + KnownMessage(*c) : std::wstring();
    if (text == messageShown_) return;
    messageShown_ = std::move(text);
    SetReadOnlyText(message_, messageShown_);
}

void LogFrame::ApplyCommitDetails(CommitDetails&& details) {
    detail_ = std::move(details);
    ShowCommitMessage();

    changes_->ResetRows(detail_.changes.size());

    insertions_ = 0;
    deletions_  = 0;
    for (const FileChange& fc : detail_.changes) {
        insertions_ += std::max(fc.insertions, 0);
        deletions_  += std::max(fc.deletions, 0);
    }
    if (const long row = SelectedIndex(); row >= 0) commits_->RefreshRow(row);
}

void LogFrame::ClearDetailPanes() {
    detail_.changes.clear();
    changes_->ResetRows(0);
    ShowCommitMessage();
}

int LogFrame::BeginDetailLoad() {
    const int token = ++nextToken_;
    canceller_.Cancel();
    loadPending_ = true;
    RefreshStatus();
    return token;
}

void LogFrame::DispatchCommitLoad(const std::wstring& sha) {
    const int token = BeginDetailLoad();
    {
        std::lock_guard lock(mu_);
        pendingTok_ = token;
        pendingSha_ = sha;
    }
    cv_.notify_one();
}

void LogFrame::ScheduleCommitLoad() {
    BeginDetailLoad();
    ClearDetailPanes();
    loadTimer_.StartOnce(kLoadDelayMs);
}

void LogFrame::ReloadSelectedCommit() {
    ShowCommitMessage();
    if (const std::wstring sha = SelectedSha(); !sha.empty()) {
        DispatchCommitLoad(sha);
    }
}

void LogFrame::OnDetailLoaded(int token,
                              const std::shared_ptr<CommitDetails>& details) {
    if (token != nextToken_.load()) return;
    ApplyCommitDetails(std::move(*details));
    loadPending_ = false;
    RefreshStatus();
}

void LogFrame::AttachLoader() {
    shown_ = params_.loader->AcknowledgeCount();
    params_.loader->Notify([this] { CallAfter([this] { AppendLoadedCommits(); }); });
}

void LogFrame::AppendLoadedCommits() {
    if (!params_.loader) return;
    const size_t count = params_.loader->AcknowledgeCount();
    if (count == shown_) return;
    shown_ = count;
    commits_->SetRowCount(shown_);
}

void LogFrame::RequestCommitsNear(long row) {
    if (!params_.loader || row < 0) return;
    const size_t next = static_cast<size_t>(row) + 1;
    if (next + kCommitPage / 2 >= CommitCount()) {
        params_.loader->Request(next + kCommitPage);
    }
}

void LogFrame::ReloadCommitList() {
    branch_ = QueryBranchLabel(params_.logArgs, params_.cwd);
    SetTitle(ComposeTitle(params_, branch_));

    const long         keepRow = SelectedIndex();
    const std::wstring keepSha = SelectedSha();

    CommitListResult lr = StartCommitLog(
        params_.logArgs, params_.cwd,
        static_cast<size_t>(keepRow + 1) + kCommitPage);
    if (!lr.errorMessage.empty()) {
        ShowError(this, L"Reload failed", lr.errorMessage);
        return;
    }
    params_.loader = std::move(lr.loader);
    AttachLoader();
    commits_->ResetRows(shown_);

    if (shown_ == 0) {
        ClearDetailPanes();
        return;
    }

    const size_t found = params_.loader->IndexOf(keepSha);
    const long   row   = (found < shown_) ? static_cast<long>(found) : 0;
    commits_->SelectOnly(row);
    ReloadSelectedCommit();
}

void LogFrame::WorkerLoop() {
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
            LoadCommitDetails(sha, params_.repoRoot, &canceller_));
        std::lock_guard lock(mu_);
        if (stop_) return;
        CallAfter([this, token, details] { OnDetailLoaded(token, details); });
    }
}

void LogFrame::StopDetailWorker() {
    {
        std::lock_guard lock(mu_);
        stop_ = true;
    }
    canceller_.Cancel();
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void LogFrame::OpenDiffForSelection() {
    const std::vector<const FileChange*> selection = SelectedChanges();
    const std::optional<Commit> c = SelectedCommit();
    if (selection.empty() || !c) return;

    std::vector<std::wstring> paths;
    for (const FileChange* fc : selection) {
        paths.push_back(fc->path);
        if (!fc->oldPath.empty() && fc->oldPath != fc->path) {
            paths.push_back(fc->oldPath);
        }
    }

    DiffWindowParams p;
    p.title = L"Diff: " +
              (selection.size() == 1 ? selection.front()->path
                                     : std::to_wstring(selection.size()) +
                                           L" files") +
              L" - " + c->shortSha;
    p.diffText = SeparateFileDiffs(
        LoadFilesDiff(c->fullSha, paths, params_.repoRoot));
    p.workTree = params_.workTree;
    ShowDiffWindow(this, p);
}

void LogFrame::CopySelection(bool commits) {
    std::wstring text;
    if (commits) {
        text = SelectedSha();
    } else {
        std::vector<std::wstring> paths;
        for (const FileChange* fc : SelectedChanges()) paths.push_back(fc->path);
        text = Join(paths, L"\r\n");
    }
    if (!text.empty()) SetClipboardText(text);
}

void LogFrame::OnListKey(bool commits, wxKeyEvent& event) {
    const bool ctrl = event.GetModifiers() == wxMOD_CONTROL;
    const int  key  = event.GetKeyCode();

    if (ctrl && key == 'A' && !commits) {
        changes_->SelectAllRows();
    } else if (key == WXK_F5 && event.GetModifiers() == wxMOD_NONE) {
        if (commits) ReloadCommitList();
        else         ReloadSelectedCommit();
    } else if (ctrl && key == 'C') {
        CopySelection(commits);
    } else {
        event.Skip();
    }
}

void LogFrame::ShowCommitsContextMenu(const wxPoint& at) {
    const std::optional<Commit> c = SelectedCommit();
    if (!c) return;

    switch (ChooseFromMenu(commits_, at, {
                {kCmdCopyHash,    L"Copy &hash"},
                {kCmdCopyMessage, L"Copy commit &message"},
                {kCmdCopyAuthor,  L"Copy &author"},
                {kCmdCopyEmail,   L"Copy author &email"},
            })) {
        case kCmdCopyHash:
            SetClipboardText(c->fullSha);
            break;
        case kCmdCopyMessage:
            SetClipboardText(FullMessage(*c));
            break;
        case kCmdCopyAuthor:
            SetClipboardText(c->authorEmail.empty()
                                 ? c->author
                                 : c->author + L" <" + c->authorEmail + L">");
            break;
        case kCmdCopyEmail:
            SetClipboardText(c->authorEmail);
            break;
    }
}

void LogFrame::ShowChangesContextMenu(const wxPoint& at) {
    const FileChange* fc = SelectedChange();
    if (!fc) return;

    const std::wstring path   = RepoFilePath(params_.workTree, fc->path);
    const std::wstring editor = FindEditor();

    switch (ChooseFromMenu(changes_, at, {
                {kCmdOpenLocation, L"&Open file location",
                 PathExists(ParentDirectory(path))},
                {kCmdEditFile, editor.empty() ? nullptr : L"&Edit file",
                 PathExists(path)},
            })) {
        case kCmdOpenLocation:
            if (!RevealInExplorer(path)) {
                ShowCouldNotOpen(this, L"Open file location", path);
            }
            break;
        case kCmdEditFile:
            if (!OpenWithEditor(editor, path)) {
                ShowCouldNotOpen(this, L"Edit file", path);
            }
            break;
    }
}

}

int ShowLogWindow(const RepoContext& repo, std::wstring query,
                  std::vector<std::wstring> logArgs,
                  CommitListResult log) {
    LogWindowParams p;
    p.query    = std::move(query);
    p.repoRoot = repo.root;
    p.workTree = repo.workTree;
    p.cwd      = repo.cwd;
    p.logArgs  = std::move(logArgs);
    p.loader   = std::move(log.loader);
    return ShowLogWindow(std::move(p));
}

int ShowLogWindow(LogWindowParams params) {
    ShowOnActiveDisplay(new LogFrame(std::move(params)));
    return 0;
}

}

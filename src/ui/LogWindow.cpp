#include "ui/LogWindow.hpp"

#include "git/Config.hpp"
#include "git/Git.hpp"

#include "ui/Announce.hpp"
#include "ui/App.hpp"
#include "ui/ChangeList.hpp"
#include "ui/Columns.hpp"
#include "ui/DateFilterDialog.hpp"
#include "ui/ListView.hpp"
#include "ui/ToolFrame.hpp"
#include "ui/Widgets.hpp"

#include <wx/panel.h>
#include <wx/timer.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace git_tools {

namespace {

struct LogWindowParams {
    RepoContext                   repo;
    std::wstring                  query;
    std::vector<std::wstring>     logArgs;
    std::unique_ptr<CommitLoader> loader;
};

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

std::wstring DescribeDates(const DateRange& dates) {
    std::vector<std::wstring> parts;
    if (!dates.since.empty()) parts.push_back(L"from " + dates.since);
    if (!dates.until.empty()) parts.push_back(L"to " + dates.until);
    return Join(parts, L" ");
}

std::wstring ComposeTitle(const LogWindowParams& p, const DateRange& dates,
                          const std::wstring& branch) {
    std::wstring t = L"gittools";
    if (!p.query.empty()) t += L" - " + p.query;
    if (!dates.empty()) t += L" - " + DescribeDates(dates);
    if (!branch.empty() && !QueryMentions(p.query, branch)) t += L" - " + branch;
    if (!p.repo.root.empty()) t += L" - " + p.repo.root;
    return t;
}

class LogFrame : public ToolFrame {
public:
    explicit LogFrame(LogWindowParams params);
    ~LogFrame() override;

protected:
    std::vector<MenuSection> Menus() override;
    std::wstring             StatusText() const override;
    void                     OnOptionsChanged() override;

private:
    size_t CommitCount() const { return shown_; }

    long SelectedIndex() const {
        return RowWithin(commits_->SelectedRow(), CommitCount());
    }

    std::optional<Commit> SelectedCommit() const;
    std::wstring          SelectedSha() const;

    std::wstring CommitCell(long row, long column) const;
    std::wstring KnownMessage(const Commit& c) const;
    std::wstring FullMessage(const Commit& c) const;
    const std::wstring* LoadedMessage(const Commit& c) const;

    void UpdateTitle();
    void ShowCommitMessage();
    void ApplyCommitDetails(CommitDetails&& details);
    void ClearDetailPanes();

    void BeginDetailLoad();
    void ScheduleCommitLoad();
    void ReloadSelectedCommit();
    void OnDetailLoaded(int token, const std::shared_ptr<CommitDetails>& details);

    void AttachLoader();
    void AppendLoadedCommits();
    void RequestCommitsNear(long row);
    bool ReloadCommitList();

    void EditDateFilter();
    void ApplyDateFilter(DateRange dates);

    std::optional<ChangesDiff> DiffOfChanges(const std::vector<size_t>& rows) const;
    void CopySelectedSha();
    void ShowCommitsContextMenu(const wxPoint& at);

    LogWindowParams     params_;
    VirtualList*        commits_ = nullptr;
    wxTextCtrl*         message_ = nullptr;
    ChangeList*         changes_ = nullptr;
    std::wstring        messageShown_;
    size_t              shown_      = 0;
    CommitDetails       detail_;
    long long           insertions_ = 0;
    long long           deletions_  = 0;
    wxTimer             loadTimer_;
    int                 debounceMs_  = DebounceMs();
    bool                loadPending_ = false;
    DateRange           dates_;
    CommitDetailsLoader detailLoader_;
};

LogFrame::LogFrame(LogWindowParams params)
    : ToolFrame(L"", wxSize(1100, 750)),
      params_(std::move(params)),
      detailLoader_(params_.repo.root,
                    [this](int token, std::shared_ptr<CommitDetails> details) {
                        CallAfter([this, token, details] { OnDetailLoaded(token, details); });
                    }) {
    UpdateTitle();

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
    changes_ = new ChangeList(panel, params_.repo.workTree,
                              [this](const std::vector<size_t>& rows) {
        return DiffOfChanges(rows);
    });
    AddPane(changes_, 26);
    FinishLayout(changes_, 20);
    PrepareAnnouncements(this);

    commits_->WhenSelected([this] {
        if (const long row = SelectedIndex(); row >= 0) {
            RequestCommitsNear(row);
            ScheduleCommitLoad();
        }
    });
    commits_->WhenRowsNeeded([this](long lastRow) { RequestCommitsNear(lastRow); });
    BindKey(commits_, WXK_F5, wxMOD_NONE, [this] { ReloadCommitList(); });
    BindKey(commits_, 'C', wxMOD_CONTROL, [this] { CopySelectedSha(); });
    changes_->WhenReload([this] { ReloadSelectedCommit(); });
    commits_->WhenContextMenu([this](const wxPoint& at) { ShowCommitsContextMenu(at); });
    loadTimer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { ReloadSelectedCommit(); });

    if (params_.loader) AttachLoader();
    if (CommitCount() > 0) commits_->SelectOnly(0);
    commits_->SetFocus();
}

LogFrame::~LogFrame() {
    loadTimer_.Stop();
    params_.loader.reset();
    detailLoader_.Stop();
}

std::vector<MenuSection> LogFrame::Menus() {
    std::vector<MenuSection> menus = ToolFrame::Menus();
    menus.push_back({L"&View", {
        {L"Filter by &date...\tCtrl+D", [this] { EditDateFilter(); }},
        {L"&Clear date filter\tCtrl+Shift+D", [this] { ApplyDateFilter({}); },
         !dates_.empty()},
    }});
    return menus;
}

std::wstring LogFrame::StatusText() const {
    return loadPending_ ? std::wstring(L"git show - running")
                        : ToolFrame::StatusText();
}

void LogFrame::OnOptionsChanged() {
    commits_->ApplyColumnLayout();
    changes_->ApplyColumnLayout();
    debounceMs_ = DebounceMs();
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
    std::wstring message = LoadCommitMessage(c.fullSha, params_.repo.root);
    return message.empty() ? c.subject : message;
}

void LogFrame::UpdateTitle() {
    SetTitle(ComposeTitle(params_, dates_,
                          QueryBranchLabel(params_.logArgs, params_.repo.cwd)));
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

    insertions_ = 0;
    deletions_  = 0;
    for (const FileChange& fc : detail_.changes) {
        insertions_ += std::max(fc.insertions, 0);
        deletions_  += std::max(fc.deletions, 0);
    }
    changes_->SetChanges(std::move(detail_.changes));
    if (const long row = SelectedIndex(); row >= 0) commits_->RefreshRow(row);
}

void LogFrame::ClearDetailPanes() {
    changes_->SetChanges({});
    ShowCommitMessage();
}

void LogFrame::BeginDetailLoad() {
    detailLoader_.Invalidate();
    loadPending_ = true;
    RefreshStatus();
}

void LogFrame::ScheduleCommitLoad() {
    ClearDetailPanes();
    if (debounceMs_ <= 0) {
        loadTimer_.Stop();
        ReloadSelectedCommit();
        return;
    }
    BeginDetailLoad();
    loadTimer_.StartOnce(debounceMs_);
}

void LogFrame::ReloadSelectedCommit() {
    ShowCommitMessage();
    if (const std::wstring sha = SelectedSha(); !sha.empty()) {
        BeginDetailLoad();
        detailLoader_.Load(sha);
    }
}

void LogFrame::OnDetailLoaded(int token,
                              const std::shared_ptr<CommitDetails>& details) {
    if (!detailLoader_.IsCurrent(token)) return;
    ApplyCommitDetails(std::move(*details));
    loadPending_ = false;
    RefreshStatus();
}

void LogFrame::AttachLoader() {
    shown_ = params_.loader->AcknowledgeCount();
    params_.loader->Notify([this] { CallAfter([this] { AppendLoadedCommits(); }); });
    commits_->ResetRows(shown_);
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

bool LogFrame::ReloadCommitList() {
    UpdateTitle();

    const long         keepRow = SelectedIndex();
    const std::wstring keepSha = SelectedSha();

    CommitListResult lr = StartCommitLog(
        WithDateRange(params_.logArgs, dates_), params_.repo.cwd,
        static_cast<size_t>(keepRow + 1) + kCommitPage);
    if (!lr.errorMessage.empty()) {
        ShowError(this, L"Reload failed", lr.errorMessage);
        return false;
    }
    params_.loader = std::move(lr.loader);
    AttachLoader();

    if (shown_ == 0) {
        ClearDetailPanes();
        return true;
    }

    const size_t found = params_.loader->IndexOf(keepSha);
    const long   row   = (found < shown_) ? static_cast<long>(found) : 0;
    commits_->SelectOnly(row);
    ReloadSelectedCommit();
    return true;
}

void LogFrame::EditDateFilter() {
    DateRange dates = dates_;
    if (ShowDateFilterDialog(this, dates)) ApplyDateFilter(std::move(dates));
}

void LogFrame::ApplyDateFilter(DateRange dates) {
    if (dates == dates_) return;
    std::swap(dates_, dates);
    if (!ReloadCommitList()) {
        dates_ = std::move(dates);
        UpdateTitle();
        return;
    }
    if (CommitCount() == 0) Announce(commits_, L"No commits in the selected date range");
}

std::optional<ChangesDiff> LogFrame::DiffOfChanges(
    const std::vector<size_t>& rows) const {
    const std::optional<Commit> c = SelectedCommit();
    if (!c) return std::nullopt;

    std::vector<std::wstring> paths;
    for (size_t row : rows) {
        const FileChange& fc = changes_->Changes()[row];
        paths.push_back(fc.path);
        if (!fc.oldPath.empty() && fc.oldPath != fc.path) {
            paths.push_back(fc.oldPath);
        }
    }
    return ChangesDiff{LoadFilesDiff(c->fullSha, paths, params_.repo.root),
                       c->shortSha};
}

void LogFrame::CopySelectedSha() {
    if (const std::wstring sha = SelectedSha(); !sha.empty()) SetClipboardText(sha);
}

void LogFrame::ShowCommitsContextMenu(const wxPoint& at) {
    const std::optional<Commit> c = SelectedCommit();
    if (!c) return;

    const auto copy = [](std::wstring text) {
        return [text = std::move(text)] { SetClipboardText(text); };
    };
    ShowPopupMenu(commits_, at, {
        {L"Copy &hash", copy(c->fullSha)},
        {L"Copy commit &message", [this, &c] { SetClipboardText(FullMessage(*c)); }},
        {L"Copy &author", copy(c->authorEmail.empty()
                                   ? c->author
                                   : c->author + L" <" + c->authorEmail + L">")},
        {L"Copy author &email", copy(c->authorEmail)},
    });
}

}

int ShowLogWindow(const RepoContext& repo, std::wstring query,
                  std::vector<std::wstring> logArgs,
                  CommitListResult log) {
    ShowOnActiveDisplay(new LogFrame(
        {repo, std::move(query), std::move(logArgs), std::move(log.loader)}));
    return 0;
}

}

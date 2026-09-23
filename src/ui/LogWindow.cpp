#include "ui/LogWindow.hpp"

#include "git/Git.hpp"
#include "ui/AppMenu.hpp"
#include "ui/DialogUtil.hpp"
#include "ui/DiffWindow.hpp"
#include "ui/OutputPane.hpp"
#include "ui/Shell.hpp"

#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace git_tools {

namespace {

constexpr int  kIdCommitList  = 1001;
constexpr int  kIdMessageEdit = 1002;
constexpr int  kIdChangesList = 1003;
constexpr int  kIdOutputEdit  = 1004;
constexpr int  kIdStatusBar   = 1005;
constexpr UINT WM_GITTOOLS_DETAIL  = WM_GITTOOLS_WINDOW;
constexpr UINT WM_GITTOOLS_COMMITS = WM_GITTOOLS_WINDOW + 1;

constexpr UINT_PTR kLoadTimerId = 1;
constexpr UINT     kLoadDelayMs = 250;

constexpr int kCmdOpenLocation = 5001;
constexpr int kCmdEditFile     = 5002;
constexpr int kCmdCopyHash     = 5101;
constexpr int kCmdCopyMessage  = 5102;
constexpr int kCmdCopyAuthor   = 5103;
constexpr int kCmdCopyEmail    = 5104;

constexpr int kColumnInsertions = 3;

struct LogWindowData {
    LogWindowParams          params;
    std::wstring             branch;
    HWND                     hwnd         = nullptr;
    HWND                     hCommitLabel = nullptr;
    HWND                     hCommitList  = nullptr;
    HWND                     hMsgLabel    = nullptr;
    HWND                     hMsgEdit     = nullptr;
    HWND                     hChgLabel    = nullptr;
    HWND                     hChgList     = nullptr;
    OutputPane               out;
    size_t                   shown        = 0;
    std::wstring             dispText;
    CommitDetails            detail;
    long long                insertions   = 0;
    long long                deletions    = 0;

    bool                     loadPending  = false;
    ProcessCanceller         canceller;
    std::thread              worker;
    std::mutex               mu;
    std::condition_variable  cv;
    bool                     stop         = false;
    int                      pendingTok   = -1;
    std::wstring             pendingSha;
    std::atomic<int>         nextToken{0};

    size_t commitCount() const { return shown; }

    int selectedIndex() const {
        return SelectedIndexIn(hCommitList, commitCount());
    }

    std::optional<Commit> selectedCommit() const {
        const int i = selectedIndex();
        if (i < 0) return std::nullopt;
        return params.loader->At(static_cast<size_t>(i));
    }

    std::wstring selectedSha() const {
        const int i = selectedIndex();
        return (i < 0) ? std::wstring()
                       : params.loader->Sha(static_cast<size_t>(i));
    }

    const FileChange* selectedChange() const {
        const int i = SelectedIndexIn(hChgList, detail.changes.size());
        return (i < 0) ? nullptr : &detail.changes[i];
    }

    std::vector<const FileChange*> selectedChanges() const {
        std::vector<const FileChange*> out;
        for (int i : SelectedRows(hChgList)) {
            if (static_cast<size_t>(i) < detail.changes.size()) {
                out.push_back(&detail.changes[i]);
            }
        }
        return out;
    }
};

bool IsLogList(UINT_PTR id) {
    return id == kIdCommitList || id == kIdChangesList;
}

std::wstring StatusFor(const LogWindowData* d) {
    return d->loadPending ? std::wstring(L"git show - running")
                          : TranscriptStatus();
}

void RefreshStatus(LogWindowData* d) {
    d->out.SetStatusText(StatusFor(d));
}

void RefreshTranscript(LogWindowData* d) {
    d->out.Refresh(StatusFor(d));
}

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

void RefreshTitle(LogWindowData* d, HWND hwnd) {
    d->branch = QueryBranchLabel(d->params.logArgs, d->params.cwd);
    SetWindowTextW(hwnd, ComposeTitle(d->params, d->branch).c_str());
}

bool SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (auto* dst = mem ? static_cast<wchar_t*>(GlobalLock(mem)) : nullptr) {
        memcpy(dst, text.c_str(), bytes);
        GlobalUnlock(mem);
        ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    }
    if (mem && !ok) GlobalFree(mem);
    CloseClipboard();
    return ok;
}

std::wstring SeparateFileDiffs(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size() + 64);
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t eol  = text.find(L'\n', pos);
        const size_t next = (eol == text.npos) ? text.size() : eol + 1;
        if (!out.empty() && text.substr(pos).starts_with(L"diff --git ")) {
            if (out.back() != L'\n') out += L'\n';
            out += L"\n\n";
        }
        out += text.substr(pos, next - pos);
        pos = next;
    }
    return out;
}

void OpenDiffForSelection(LogWindowData* d, HWND owner) {
    const std::vector<const FileChange*> selection = d->selectedChanges();
    const std::optional<Commit> c = d->selectedCommit();
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
        LoadFilesDiff(c->fullSha, paths, d->params.repoRoot));
    p.workTree = d->params.workTree;
    ShowDiffWindow(owner, p);
}

std::wstring FormatCount(int value, bool suppressed) {
    if (suppressed)  return L"";
    if (value == -2) return L"bin";
    return value < 0 ? std::wstring() : std::to_wstring(value);
}

const std::wstring* LoadedMessage(const LogWindowData* d, const Commit& c) {
    return (c.fullSha == d->detail.sha && !d->detail.message.empty())
               ? &d->detail.message
               : nullptr;
}

std::wstring KnownMessage(const LogWindowData* d, const Commit& c) {
    const std::wstring* loaded = LoadedMessage(d, c);
    return loaded ? *loaded : c.subject;
}

std::wstring FullMessage(const LogWindowData* d, const Commit& c) {
    if (const std::wstring* loaded = LoadedMessage(d, c)) return *loaded;
    std::wstring message = LoadCommitMessage(c.fullSha, d->params.repoRoot);
    return message.empty() ? c.subject : message;
}

void ShowCommitMessage(LogWindowData* d) {
    const std::optional<Commit> c = d->selectedCommit();
    const std::wstring text =
        c ? c->fullSha + L"\n\n" + KnownMessage(d, *c) : std::wstring();
    if (ControlText(d->hMsgEdit) != NormalizeCRLF(text)) {
        SetReadOnlyText(d->hMsgEdit, text);
    }
}

void ApplyCommitDetails(LogWindowData* d, CommitDetails&& details) {
    d->detail = std::move(details);
    ShowCommitMessage(d);

    SendMessageW(d->hChgList, LVM_DELETEALLITEMS, 0, 0);
    SetRowCount(d->hChgList, d->detail.changes.size());

    d->insertions = 0;
    d->deletions  = 0;
    for (const FileChange& fc : d->detail.changes) {
        d->insertions += std::max(fc.insertions, 0);
        d->deletions  += std::max(fc.deletions, 0);
    }
    if (const int row = d->selectedIndex(); row >= 0) {
        ListView_RedrawItems(d->hCommitList, row, row);
    }
}

void ClearDetailPanes(LogWindowData* d) {
    SendMessageW(d->hChgList, LVM_DELETEALLITEMS, 0, 0);
    d->detail.changes.clear();
    ShowCommitMessage(d);
}

std::wstring CommitCell(LogWindowData* d, int row, int column) {
    Commit c = d->params.loader->At(static_cast<size_t>(row));
    switch (column) {
        case 0: return std::move(c.subject);
        case 1: return std::move(c.author);
        case 2: return std::move(c.date);
        case 3:
        case 4:
            if (c.fullSha != d->detail.sha || row != d->selectedIndex()) return {};
            return std::to_wstring(column == kColumnInsertions ? d->insertions
                                                               : d->deletions);
        default: return {};
    }
}

std::wstring ChangeName(const FileChange& fc) {
    const bool twoPaths = fc.kind == FileChangeKind::Renamed ||
                          fc.kind == FileChangeKind::Copied;
    return (twoPaths && !fc.oldPath.empty()) ? fc.oldPath + L" -> " + fc.path
                                             : fc.path;
}

std::wstring ChangeCell(const FileChange& fc, int column) {
    switch (column) {
        case 0: return ChangeName(fc);
        case 1: return std::wstring(1, fc.kindChar);
        case 2: return FormatCount(fc.insertions,
                                   fc.kind == FileChangeKind::Deleted);
        case 3: return FormatCount(fc.deletions,
                                   fc.kind == FileChangeKind::Added);
        default: return {};
    }
}

int BeginDetailLoad(LogWindowData* d) {
    const int token = ++d->nextToken;
    d->canceller.Cancel();
    d->loadPending = true;
    RefreshStatus(d);
    return token;
}

void DispatchCommitLoad(LogWindowData* d, const std::wstring& sha) {
    const int token = BeginDetailLoad(d);
    {
        std::lock_guard lock(d->mu);
        d->pendingTok = token;
        d->pendingSha = sha;
    }
    d->cv.notify_one();
}

void ScheduleCommitLoad(LogWindowData* d, HWND hwnd) {
    BeginDetailLoad(d);
    ClearDetailPanes(d);
    SetTimer(hwnd, kLoadTimerId, kLoadDelayMs, nullptr);
}

void ReloadSelectedCommit(LogWindowData* d) {
    ShowCommitMessage(d);
    if (const std::wstring sha = d->selectedSha(); !sha.empty()) {
        DispatchCommitLoad(d, sha);
    }
}

void AttachLoader(LogWindowData* d, HWND hwnd) {
    d->shown = d->params.loader->AcknowledgeCount();
    d->params.loader->Notify(hwnd, WM_GITTOOLS_COMMITS);
}

void AppendLoadedCommits(LogWindowData* d) {
    if (!d->params.loader) return;
    const size_t count = d->params.loader->AcknowledgeCount();
    if (count == d->shown) return;
    d->shown = count;
    SetRowCount(d->hCommitList, d->shown, LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
}

void RequestCommitsNear(LogWindowData* d, int row) {
    if (!d->params.loader || row < 0) return;
    const size_t next = static_cast<size_t>(row) + 1;
    if (next + kCommitPage / 2 >= d->commitCount()) {
        d->params.loader->Request(next + kCommitPage);
    }
}

void ReloadCommitList(LogWindowData* d, HWND hwnd) {
    RefreshTitle(d, hwnd);

    const int          keepRow = d->selectedIndex();
    const std::wstring keepSha = d->selectedSha();

    CommitListResult lr = StartCommitLog(
        d->params.logArgs, d->params.cwd,
        static_cast<size_t>(keepRow + 1) + kCommitPage);
    if (!lr.errorMessage.empty()) {
        ShowError(hwnd, L"Reload failed", lr.errorMessage);
        return;
    }
    d->params.loader = std::move(lr.loader);
    AttachLoader(d, hwnd);
    SetRowCount(d->hCommitList, d->shown, LVSICF_NOSCROLL);
    InvalidateRect(d->hCommitList, nullptr, TRUE);

    if (d->shown == 0) {
        ClearDetailPanes(d);
        return;
    }

    const size_t found = d->params.loader->IndexOf(keepSha);
    const int    row   = (found < d->shown) ? static_cast<int>(found) : 0;
    SelectRow(d->hCommitList, row);
    ListView_EnsureVisible(d->hCommitList, row, FALSE);
    ReloadSelectedCommit(d);
}

void WorkerLoop(LogWindowData* d) {
    for (;;) {
        int          token;
        std::wstring sha;
        {
            std::unique_lock lock(d->mu);
            d->cv.wait(lock, [d] { return d->stop || d->pendingTok >= 0; });
            if (d->stop) return;
            token = std::exchange(d->pendingTok, -1);
            sha   = d->pendingSha;
        }

        d->canceller.Reset();
        auto* details = new CommitDetails(
            LoadCommitDetails(sha, d->params.repoRoot, &d->canceller));
        {
            std::lock_guard lock(d->mu);
            if (d->stop) {
                delete details;
                return;
            }
        }
        PostMessageW(d->hwnd, WM_GITTOOLS_DETAIL, static_cast<WPARAM>(token),
                     reinterpret_cast<LPARAM>(details));
    }
}

void StopDetailWorker(LogWindowData* d, HWND hwnd) {
    {
        std::lock_guard lock(d->mu);
        d->stop = true;
    }
    d->canceller.Cancel();
    d->cv.notify_all();
    if (d->worker.joinable()) d->worker.join();

    MSG pending;
    while (PeekMessageW(&pending, hwnd, WM_GITTOOLS_DETAIL, WM_GITTOOLS_DETAIL,
                        PM_REMOVE)) {
        delete reinterpret_cast<CommitDetails*>(pending.lParam);
    }
}

void LayoutChildren(LogWindowData* d, HWND hwnd) {
    constexpr int kMargin  = 4;
    constexpr int kLabelH  = 18;
    constexpr int kSpacing = 4;

    RECT rc;
    GetClientRect(hwnd, &rc);
    const int  cx       = rc.right;
    const int  cy       = rc.bottom;
    const bool showOut  = d->out.visible();
    const int  sections = showOut ? 4 : 3;
    const int  availH   = std::max(
        80, cy - d->out.StatusHeight() - sections * kLabelH -
                (sections - 1) * kSpacing - 2 * kMargin);
    const int topH = (availH * 36) / 100;
    const int midH = (availH * 18) / 100;
    const int outH = showOut ? (availH * 20) / 100 : 0;

    StackLayout s{kMargin, kMargin, cx - 2 * kMargin};
    s.Place(d->hCommitLabel, kLabelH);
    s.Place(d->hCommitList,  topH);
    s.Gap(kSpacing);
    s.Place(d->hMsgLabel,    kLabelH);
    s.Place(d->hMsgEdit,     midH);
    s.Gap(kSpacing);
    s.Place(d->hChgLabel,    kLabelH);
    s.Place(d->hChgList,     availH - topH - midH - outH);
    if (showOut) {
        s.Gap(kSpacing);
        s.Place(d->out.label, kLabelH);
        s.Place(d->out.edit,  outH);
    }
}

void CreateChildren(LogWindowData* d, HWND hwnd) {
    d->hCommitLabel = CreateLabel(hwnd, L"&Commits");
    d->hCommitList  = CreateListView(hwnd, kIdCommitList,
                                     LVS_SINGLESEL | LVS_OWNERDATA,
                                     LVS_EX_HEADERDRAGDROP);
    d->hMsgLabel = CreateLabel(hwnd, L"&Message");
    d->hMsgEdit  = CreateReadOnlyEdit(hwnd, kIdMessageEdit, WS_TABSTOP);
    d->hChgLabel = CreateLabel(hwnd, L"C&hanges");
    d->hChgList  = CreateListView(hwnd, kIdChangesList, LVS_OWNERDATA);
    d->out.Create(hwnd, kIdOutputEdit, kIdStatusBar);
    EnableSelectAll(d->hMsgEdit);

    const Column commitCols[] = {
        {L"Subject",    520},
        {L"Author",     160},
        {L"Date",       140},
        {L"Insertions",  80, LVCFMT_RIGHT},
        {L"Deletions",   80, LVCFMT_RIGHT},
    };
    InsertColumns(d->hCommitList, commitCols);
    SetRowCount(d->hCommitList, d->commitCount(),
                LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);

    const Column changeCols[] = {
        {L"Name",       540},
        {L"State",       60},
        {L"Insertions",  80, LVCFMT_RIGHT},
        {L"Deletions",   80, LVCFMT_RIGHT},
    };
    InsertColumns(d->hChgList, changeCols);
}

bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix) {
    if (text.size() < prefix.size()) return false;
    return prefix.empty() ||
           CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                          text.c_str(),   static_cast<int>(prefix.size()),
                          prefix.c_str(), static_cast<int>(prefix.size())) ==
               CSTR_EQUAL;
}

template <typename TextAt>
int FindByPrefix(const NMLVFINDITEMW* fi, size_t total, TextAt&& textAt) {
    if (!fi->lvfi.psz ||
        !(fi->lvfi.flags & (LVFI_STRING | LVFI_PARTIAL | LVFI_SUBSTRING))) {
        return -1;
    }

    const std::wstring prefix = fi->lvfi.psz;
    const int count = static_cast<int>(total);
    if (count == 0) return -1;

    const int start = std::clamp(fi->iStart, 0, count);
    const int span  = (fi->lvfi.flags & LVFI_WRAP) ? count : count - start;

    std::wstring text;
    for (int n = 0; n < span; ++n) {
        const int i = (start + n) % count;
        if (textAt(static_cast<size_t>(i), text) &&
            StartsWithNoCase(text, prefix)) {
            return i;
        }
    }
    return -1;
}

int FindInList(LogWindowData* d, UINT_PTR listId, const NMLVFINDITEMW* fi) {
    if (listId == kIdCommitList) {
        return FindByPrefix(fi, d->commitCount(),
                            [d](size_t i, std::wstring& text) {
                                return d->params.loader->Subject(i, text);
                            });
    }
    return FindByPrefix(fi, d->detail.changes.size(),
                        [d](size_t i, std::wstring& text) {
                            text = ChangeName(d->detail.changes[i]);
                            return true;
                        });
}

bool ContextMenuAnchor(HWND list, LPARAM lParam, POINT& pt) {
    if (lParam == static_cast<LPARAM>(-1)) {
        const int idx = SelectedRow(list);
        if (idx < 0) return false;
        RECT rc{};
        ListView_GetItemRect(list, idx, &rc, LVIR_LABEL);
        pt = {rc.left, rc.bottom};
        ClientToScreen(list, &pt);
        return true;
    }
    pt = {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
    LVHITTESTINFO ht{};
    ht.pt = pt;
    ScreenToClient(list, &ht.pt);
    const int hit = ListView_SubItemHitTest(list, &ht);
    if (hit < 0) return false;
    if (!RowSelected(list, hit)) SelectOnlyRow(list, hit);
    return true;
}

void ShowCommitsContextMenu(LogWindowData* d, HWND owner, LPARAM lParam) {
    POINT pt;
    if (!ContextMenuAnchor(d->hCommitList, lParam, pt)) return;
    const std::optional<Commit> c = d->selectedCommit();
    if (!c) return;

    switch (TrackMenu(owner, pt, {
                {kCmdCopyHash,    L"Copy &hash"},
                {kCmdCopyMessage, L"Copy commit &message"},
                {kCmdCopyAuthor,  L"Copy &author"},
                {kCmdCopyEmail,   L"Copy author &email"},
            })) {
        case kCmdCopyHash:
            SetClipboardText(owner, c->fullSha);
            break;
        case kCmdCopyMessage:
            SetClipboardText(owner, FullMessage(d, *c));
            break;
        case kCmdCopyAuthor:
            SetClipboardText(owner, c->authorEmail.empty()
                                        ? c->author
                                        : c->author + L" <" + c->authorEmail +
                                              L">");
            break;
        case kCmdCopyEmail:
            SetClipboardText(owner, c->authorEmail);
            break;
    }
}

void ShowChangesContextMenu(LogWindowData* d, HWND owner, LPARAM lParam) {
    POINT pt;
    if (!ContextMenuAnchor(d->hChgList, lParam, pt)) return;
    const FileChange* fc = d->selectedChange();
    if (!fc) return;

    const std::wstring path   = RepoFilePath(d->params.workTree, fc->path);
    const std::wstring editor = FindEditor();

    switch (TrackMenu(owner, pt, {
                {kCmdOpenLocation, L"&Open file location",
                 PathExists(ParentDirectory(path))},
                {kCmdEditFile, editor.empty() ? nullptr : L"&Edit file",
                 PathExists(path)},
            })) {
        case kCmdOpenLocation:
            if (!RevealInExplorer(owner, path)) {
                ShowCouldNotOpen(owner, L"Open file location", path);
            }
            break;
        case kCmdEditFile:
            if (!OpenWithEditor(owner, editor, path)) {
                ShowCouldNotOpen(owner, L"Edit file", path);
            }
            break;
    }
}

bool OnListKeyDown(LogWindowData* d, HWND hwnd, UINT_PTR listId, WORD vkey) {
    const bool ctrl    = CtrlPressed();
    const bool commits = listId == kIdCommitList;

    if (ctrl && vkey == 'A' && !commits) {
        SelectAllRows(d->hChgList);
        return true;
    }
    if (vkey == VK_F5) {
        if (commits) ReloadCommitList(d, hwnd);
        else         ReloadSelectedCommit(d);
        return true;
    }
    if (ctrl && vkey == 'C') {
        std::wstring text;
        if (commits) {
            text = d->selectedSha();
        } else {
            std::vector<std::wstring> paths;
            for (const FileChange* fc : d->selectedChanges()) {
                paths.push_back(fc->path);
            }
            text = Join(paths, L"\r\n");
        }
        if (!text.empty()) SetClipboardText(hwnd, text);
        return true;
    }
    return false;
}

void OnGetDispInfo(LogWindowData* d, UINT_PTR listId, NMLVDISPINFOW* di) {
    const int  row     = di->item.iItem;
    const bool commits = listId == kIdCommitList;
    if (commits) RequestCommitsNear(d, row);

    const size_t count = commits ? d->commitCount() : d->detail.changes.size();
    if (row < 0 || static_cast<size_t>(row) >= count ||
        !(di->item.mask & LVIF_TEXT)) {
        return;
    }
    d->dispText = commits ? CommitCell(d, row, di->item.iSubItem)
                          : ChangeCell(d->detail.changes[row], di->item.iSubItem);
    di->item.pszText = d->dispText.data();
}

bool OnNotify(LogWindowData* d, HWND hwnd, LPARAM lParam) {
    const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
    if (!IsLogList(nm->idFrom)) return false;

    switch (nm->code) {
        case LVN_GETDISPINFO:
            OnGetDispInfo(d, nm->idFrom, reinterpret_cast<NMLVDISPINFOW*>(lParam));
            return false;
        case LVN_ODFINDITEM:
            SetWindowLongPtrW(
                hwnd, DWLP_MSGRESULT,
                FindInList(d, nm->idFrom,
                           reinterpret_cast<const NMLVFINDITEMW*>(lParam)));
            return true;
        case LVN_KEYDOWN:
            return OnListKeyDown(
                d, hwnd, nm->idFrom,
                reinterpret_cast<const NMLVKEYDOWN*>(lParam)->wVKey);
        case LVN_ITEMACTIVATE:
            if (nm->idFrom == kIdChangesList) OpenDiffForSelection(d, hwnd);
            return false;
        case LVN_ITEMCHANGED: {
            const auto* nlv = reinterpret_cast<const NMLISTVIEW*>(lParam);
            const bool nowSelected = (nlv->uChanged & LVIF_STATE) &&
                                     (nlv->uNewState & LVIS_SELECTED) &&
                                     !(nlv->uOldState & LVIS_SELECTED);
            if (nm->idFrom == kIdCommitList && nowSelected && nlv->iItem >= 0 &&
                static_cast<size_t>(nlv->iItem) < d->commitCount()) {
                ScheduleCommitLoad(d, hwnd);
            }
            return false;
        }
        default:
            return false;
    }
}

bool OnCommand(LogWindowData* d, HWND hwnd, WPARAM wParam) {
    if (HandleFileMenuCommand(hwnd, wParam)) {
        if (LOWORD(wParam) == kCmdOptions && d->params.loader) {
            d->params.loader->SetUnloadFar(
                ConfigGetBool(kUnloadFarCommitsKey, false));
        }
        return true;
    }
    switch (LOWORD(wParam)) {
        case kCmdDebugOutput:
            ToggleDebugOutput(hwnd, d->out, kIdOutputEdit);
            RefreshTranscript(d);
            LayoutChildren(d, hwnd);
            return true;
        case IDOK:
            if (GetFocus() == d->hChgList) OpenDiffForSelection(d, hwnd);
            return true;
        default:
            return false;
    }
}

INT_PTR CALLBACK LogDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<LogWindowData>(hwnd, msg, lParam);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;
    if (!d) return FALSE;

    switch (msg) {
        case WM_INITDIALOG:
            d->hwnd = hwnd;
            if (d->params.loader) AttachLoader(d, hwnd);
            CreateChildren(d, hwnd);
            AttachFileMenu(hwnd);
            RefreshTranscript(d);

            d->worker = std::thread(WorkerLoop, d);
            if (d->commitCount() > 0) SelectRow(d->hCommitList, 0);

            LayoutChildren(d, hwnd);
            SetFocus(d->hCommitList);
            PostMessageW(hwnd, WM_GITTOOLS_ACTIVATE, 0, 0);
            return FALSE;
        case WM_SIZE:
            LayoutChildren(d, hwnd);
            return FALSE;
        case WM_TIMER:
            if (wParam != kLoadTimerId) return FALSE;
            KillTimer(hwnd, kLoadTimerId);
            ReloadSelectedCommit(d);
            return TRUE;
        case WM_GITTOOLS_ACTIVATE:
            BringDialogToFront(hwnd);
            return TRUE;
        case WM_GITTOOLS_TRANSCRIPT:
            RefreshTranscript(d);
            return TRUE;
        case WM_GITTOOLS_COMMITS:
            AppendLoadedCommits(d);
            return TRUE;
        case WM_GITTOOLS_DETAIL: {
            auto* details = reinterpret_cast<CommitDetails*>(lParam);
            if (static_cast<int>(wParam) == d->nextToken.load()) {
                ApplyCommitDetails(d, std::move(*details));
                d->loadPending = false;
                RefreshStatus(d);
            }
            delete details;
            return TRUE;
        }
        case WM_CONTEXTMENU: {
            HWND from = reinterpret_cast<HWND>(wParam);
            if (from == d->hChgList)         ShowChangesContextMenu(d, hwnd, lParam);
            else if (from == d->hCommitList) ShowCommitsContextMenu(d, hwnd, lParam);
            else                             return FALSE;
            return TRUE;
        }
        case WM_NOTIFY:
            return OnNotify(d, hwnd, lParam) ? TRUE : FALSE;
        case WM_COMMAND:
            return OnCommand(d, hwnd, wParam) ? TRUE : FALSE;
        case WM_DESTROY:
            SetTranscriptTarget(nullptr, 0);
            KillTimer(hwnd, kLoadTimerId);
            d->params.loader.reset();
            StopDetailWorker(d, hwnd);
            return FALSE;
    }
    return FALSE;
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
    LogWindowData data;
    data.params = std::move(params);
    data.branch = QueryBranchLabel(data.params.logArgs, data.params.cwd);
    return RunDialog(ComposeTitle(data.params, data.branch), 640, 460,
                     nullptr, LogDlgProc, &data);
}

}

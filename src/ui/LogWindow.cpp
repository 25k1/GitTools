#include "ui/LogWindow.hpp"

#include "git/Git.hpp"
#include "ui/DiffWindow.hpp"
#include "ui/AppMenu.hpp"
#include "ui/DialogUtil.hpp"
#include "ui/OutputPane.hpp"
#include "ui/Shell.hpp"

#include <commctrl.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace git_tools {

namespace {

constexpr int  kIdCommitList     = 1001;
constexpr int  kIdMessageEdit    = 1002;
constexpr int  kIdChangesList    = 1003;
constexpr int  kIdOutputEdit     = 1004;
constexpr int  kIdStatusBar      = 1005;
constexpr UINT WM_GITTOOLS_DETAIL     = WM_APP + 1;
constexpr UINT WM_GITTOOLS_TRANSCRIPT = WM_APP + 2;
constexpr UINT WM_GITTOOLS_ACTIVATE   = WM_APP + 3;
constexpr UINT WM_GITTOOLS_COMMITS    = WM_APP + 4;

constexpr UINT_PTR kLoadTimerId = 1;
constexpr UINT     kLoadDelayMs = 250;

constexpr int kCmdOpenLocation = 5001;
constexpr int kCmdEditFile     = 5002;
constexpr int kCmdCopyHash     = 5101;
constexpr int kCmdCopyMessage  = 5102;
constexpr int kCmdCopyAuthor   = 5103;
constexpr int kCmdCopyEmail    = 5104;

struct LogWindowData {
    LogWindowParams          params;
    std::wstring             branch;
    HWND                     hCommitLabel = nullptr;
    HWND                     hCommitList  = nullptr;
    HWND                     hMsgLabel    = nullptr;
    HWND                     hMsgEdit     = nullptr;
    HWND                     hChgLabel    = nullptr;
    HWND                     hChgList     = nullptr;
    OutputPane               out;
    bool                     loadPending  = false;
    ProcessCanceller         canceller;
    std::vector<FileChange>  currentChanges;
    size_t                   shown        = 0;
    std::wstring             dispText;
    std::wstring             detailSha;
    std::wstring             detailMessage;

    HWND                     hwnd        = nullptr;
    std::thread              worker;
    std::mutex               mu;
    std::condition_variable  cv;
    bool                     stop        = false;
    int                      pendingTok  = -1;
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
        int i = SelectedIndexIn(hChgList, currentChanges.size());
        return (i < 0) ? nullptr : &currentChanges[i];
    }

    std::vector<const FileChange*> selectedChanges() const {
        std::vector<const FileChange*> out;
        for (int i : SelectedRows(hChgList)) {
            if (i >= 0 && static_cast<size_t>(i) < currentChanges.size()) {
                out.push_back(&currentChanges[i]);
            }
        }
        return out;
    }
};

std::wstring StatusFor(const LogWindowData* d) {
    return d->loadPending ? std::wstring(L"git show - running")
                          : TranscriptStatus();
}

void RefreshStatus(LogWindowData* d) {
    SetStatusText(d->out.status, StatusFor(d));
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
    if (!branch.empty() && !QueryMentions(p.query, branch)) {
        t += L" - " + branch;
    }
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
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (hMem) {
        if (auto* dst = static_cast<wchar_t*>(GlobalLock(hMem))) {
            memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(hMem);
            if (SetClipboardData(CF_UNICODETEXT, hMem)) ok = true;
            else GlobalFree(hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
    return ok;
}

std::wstring SeparateFileDiffs(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 64);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol  = text.find(L'\n', pos);
        size_t next = (eol == std::wstring::npos) ? text.size() : eol + 1;
        if (!out.empty() && text.compare(pos, 11, L"diff --git ") == 0) {
            if (out.back() != L'\n') out += L'\n';
            out += L"\n\n";
        }
        out.append(text, pos, next - pos);
        pos = next;
    }
    return out;
}

void OpenDiffForSelection(LogWindowData* d, HWND owner) {
    if (!d) return;
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
    p.title = L"Diff: ";
    p.title += (selection.size() == 1)
                   ? selection.front()->path
                   : std::to_wstring(selection.size()) + L" files";
    p.title += L" - " + c->shortSha;
    p.diffText = SeparateFileDiffs(
        LoadFilesDiff(c->fullSha, paths, d->params.repoRoot));
    p.workTree = d->params.workTree;
    ShowDiffWindow(owner, p);
}

std::wstring FormatCount(int value, bool suppressed) {
    if (suppressed)   return L"";
    if (value == -2)  return L"bin";
    if (value < 0)    return L"";
    return std::to_wstring(value);
}

std::wstring KnownMessage(const LogWindowData* d, const Commit& c) {
    if (c.fullSha == d->detailSha && !d->detailMessage.empty()) {
        return d->detailMessage;
    }
    return c.subject;
}

std::wstring FullMessage(const LogWindowData* d, const Commit& c) {
    if (c.fullSha == d->detailSha && !d->detailMessage.empty()) {
        return d->detailMessage;
    }
    std::wstring message = LoadCommitMessage(c.fullSha, d->params.repoRoot);
    return message.empty() ? c.subject : message;
}

void ShowCommitMessage(LogWindowData* d) {
    const std::optional<Commit> c = d->selectedCommit();
    const std::wstring text =
        c ? c->fullSha + L"\n\n" + KnownMessage(d, *c) : std::wstring();
    if (ControlText(d->hMsgEdit) == NormalizeCRLF(text)) return;
    SetReadOnlyText(d->hMsgEdit, text);
}

void ApplyCommitDetails(LogWindowData* d, CommitDetails&& details) {
    d->detailSha     = std::move(details.sha);
    d->detailMessage = std::move(details.message);
    ShowCommitMessage(d);

    SendMessageW(d->hChgList, LVM_DELETEALLITEMS, 0, 0);
    d->currentChanges = std::move(details.changes);
    ListView_SetItemCountEx(d->hChgList,
                            static_cast<int>(d->currentChanges.size()), 0);
}

std::wstring ChangeName(const FileChange& fc) {
    if ((fc.kind == FileChangeKind::Renamed ||
         fc.kind == FileChangeKind::Copied) && !fc.oldPath.empty()) {
        return fc.oldPath + L" -> " + fc.path;
    }
    return fc.path;
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

void ClearDetailPanes(LogWindowData* d) {
    SendMessageW(d->hChgList, LVM_DELETEALLITEMS, 0, 0);
    d->currentChanges.clear();
    ShowCommitMessage(d);
}

void DispatchCommitLoad(LogWindowData* d, const std::wstring& sha) {
    int token = ++d->nextToken;
    d->loadPending = true;
    d->canceller.Cancel();
    RefreshStatus(d);
    {
        std::lock_guard<std::mutex> lock(d->mu);
        d->pendingTok = token;
        d->pendingSha = sha;
    }
    d->cv.notify_one();
}

void ScheduleCommitLoad(LogWindowData* d, HWND hwnd) {
    ++d->nextToken;
    d->canceller.Cancel();
    d->loadPending = true;
    ClearDetailPanes(d);
    RefreshStatus(d);
    SetTimer(hwnd, kLoadTimerId, kLoadDelayMs, nullptr);
}

void ReloadSelectedCommit(LogWindowData* d) {
    ShowCommitMessage(d);
    const std::wstring sha = d->selectedSha();
    if (!sha.empty()) DispatchCommitLoad(d, sha);
}

void AppendLoadedCommits(LogWindowData* d) {
    if (!d->params.loader) return;
    const size_t count = d->params.loader->AcknowledgeCount();
    if (count == d->shown) return;
    d->shown = count;
    ListView_SetItemCountEx(d->hCommitList,
                            static_cast<int>(d->commitCount()),
                            LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
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
    d->shown         = lr.count;
    d->params.loader->Notify(hwnd, WM_GITTOOLS_COMMITS);

    ListView_SetItemCountEx(d->hCommitList,
                            static_cast<int>(d->commitCount()),
                            LVSICF_NOSCROLL);
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
        int token;
        std::wstring sha;
        {
            std::unique_lock<std::mutex> lock(d->mu);
            d->cv.wait(lock, [&] {
                return d->stop || d->pendingTok >= 0;
            });
            if (d->stop) return;
            token = d->pendingTok;
            sha   = d->pendingSha;
            d->pendingTok = -1;
        }

        d->canceller.Reset();
        auto* details = new CommitDetails(
            LoadCommitDetails(sha, d->params.repoRoot, &d->canceller));

        {
            std::lock_guard<std::mutex> lock(d->mu);
            if (d->stop) {
                delete details;
                return;
            }
        }
        PostMessageW(d->hwnd, WM_GITTOOLS_DETAIL,
                     static_cast<WPARAM>(token),
                     reinterpret_cast<LPARAM>(details));
    }
}

void LayoutChildren(LogWindowData* d, int cx, int cy) {
    const int margin   = 4;
    const int labelH   = 18;
    const int spacing  = 4;
    const bool showOut = d->out.visible();
    const int sections = showOut ? 4 : 3;

    int availH = cy - d->out.StatusHeight() -
                 sections * labelH - (sections - 1) * spacing - 2 * margin;
    if (availH < 80) availH = 80;
    int topH = (availH * 36) / 100;
    int midH = (availH * 18) / 100;
    int outH = showOut ? (availH * 20) / 100 : 0;
    int botH = availH - topH - midH - outH;

    StackLayout s{margin, margin, cx - 2 * margin};
    s.Place(d->hCommitLabel, labelH);
    s.Place(d->hCommitList,  topH);
    s.Gap(spacing);
    s.Place(d->hMsgLabel,    labelH);
    s.Place(d->hMsgEdit,     midH);
    s.Gap(spacing);
    s.Place(d->hChgLabel,    labelH);
    s.Place(d->hChgList,     botH);
    if (showOut) {
        s.Gap(spacing);
        s.Place(d->out.label, labelH);
        s.Place(d->out.edit,  outH);
    }
}

void RefreshTranscript(LogWindowData* d) {
    d->out.Refresh(StatusFor(d));
}

void CreateChildren(LogWindowData* d, HWND hwnd) {
    d->hCommitLabel = CreateLabel(hwnd, L"&Commits");
    d->hCommitList  = CreateListView(hwnd, kIdCommitList,
                                     LVS_SINGLESEL | LVS_OWNERDATA,
                                     LVS_EX_HEADERDRAGDROP);

    d->hMsgLabel = CreateLabel(hwnd, L"&Message");
    d->hMsgEdit  = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
        0, 0, 0, 0, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdMessageEdit)),
        GetModuleHandleW(nullptr), nullptr);

    d->hChgLabel = CreateLabel(hwnd, L"C&hanges");
    d->hChgList  = CreateListView(hwnd, kIdChangesList, LVS_OWNERDATA);

    d->out.Create(hwnd, kIdOutputEdit, kIdStatusBar, WM_GITTOOLS_TRANSCRIPT);
    EnableSelectAll(d->hMsgEdit);

    const Column commitCols[] = {
        {L"Subject", 520},
        {L"Author",  160},
        {L"Date",    140},
    };
    InsertColumns(d->hCommitList, commitCols);
    ListView_SetItemCountEx(d->hCommitList,
                            static_cast<int>(d->commitCount()),
                            LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);

    const Column changeCols[] = {
        {L"Name",       540, LVCFMT_LEFT },
        {L"State",       60, LVCFMT_LEFT },
        {L"Insertions",  80, LVCFMT_RIGHT},
        {L"Deletions",   80, LVCFMT_RIGHT},
    };
    InsertColumns(d->hChgList, changeCols);
}

bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix) {
    if (prefix.empty()) return true;
    if (text.size() < prefix.size()) return false;
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                          text.c_str(),   static_cast<int>(prefix.size()),
                          prefix.c_str(), static_cast<int>(prefix.size()))
           == CSTR_EQUAL;
}

template <typename TextAt>
int FindByPrefix(const NMLVFINDITEMW* fi, size_t total, TextAt&& textAt) {
    if (!fi->lvfi.psz) return -1;
    if (!(fi->lvfi.flags & (LVFI_STRING | LVFI_PARTIAL | LVFI_SUBSTRING))) {
        return -1;
    }

    const std::wstring prefix = fi->lvfi.psz;
    const int count = static_cast<int>(total);
    if (count == 0) return -1;

    int start = fi->iStart;
    if (start < 0)     start = 0;
    if (start > count) start = count;

    const bool wrap = (fi->lvfi.flags & LVFI_WRAP) != 0;
    const int  span = wrap ? count : count - start;

    std::wstring text;
    for (int n = 0; n < span; ++n) {
        int i = start + n;
        if (i >= count) i -= count;
        if (textAt(static_cast<size_t>(i), text) &&
            StartsWithNoCase(text, prefix)) {
            return i;
        }
    }
    return -1;
}

int FindCommitByPrefix(LogWindowData* d, const NMLVFINDITEMW* fi) {
    return FindByPrefix(fi, d->commitCount(),
                        [d](size_t i, std::wstring& text) {
                            return d->params.loader->Subject(i, text);
                        });
}

int FindChangeByPrefix(LogWindowData* d, const NMLVFINDITEMW* fi) {
    return FindByPrefix(fi, d->currentChanges.size(),
                        [d](size_t i, std::wstring& text) {
                            text = ChangeName(d->currentChanges[i]);
                            return true;
                        });
}

bool ContextMenuAnchor(HWND list, LPARAM lParam, POINT& pt) {
    if (lParam == static_cast<LPARAM>(-1)) {
        int idx = SelectedRow(list);
        if (idx < 0) return false;
        RECT rc{};
        ListView_GetItemRect(list, idx, &rc, LVIR_LABEL);
        pt.x = rc.left;
        pt.y = rc.bottom;
        ClientToScreen(list, &pt);
        return true;
    }
    POINT client{static_cast<short>(LOWORD(lParam)),
                 static_cast<short>(HIWORD(lParam))};
    ScreenToClient(list, &client);
    LVHITTESTINFO ht{};
    ht.pt = client;
    int hit = ListView_SubItemHitTest(list, &ht);
    if (hit < 0) return false;
    if (!RowSelected(list, hit)) SelectOnlyRow(list, hit);
    pt.x = static_cast<short>(LOWORD(lParam));
    pt.y = static_cast<short>(HIWORD(lParam));
    return true;
}

void ShowCommitsContextMenu(LogWindowData* d, HWND owner, LPARAM lParam) {
    POINT pt;
    if (!ContextMenuAnchor(d->hCommitList, lParam, pt)) return;

    const std::optional<Commit> c = d->selectedCommit();
    if (!c) return;

    int cmd = TrackMenu(owner, pt, {
        {kCmdCopyHash,    L"Copy &hash"},
        {kCmdCopyMessage, L"Copy commit &message"},
        {kCmdCopyAuthor,  L"Copy &author"},
        {kCmdCopyEmail,   L"Copy author &email"},
    });

    if (cmd == kCmdCopyHash) {
        SetClipboardText(owner, c->fullSha);
    } else if (cmd == kCmdCopyMessage) {
        SetClipboardText(owner, FullMessage(d, *c));
    } else if (cmd == kCmdCopyAuthor) {
        SetClipboardText(owner, c->authorEmail.empty()
                                    ? c->author
                                    : c->author + L" <" + c->authorEmail + L">");
    } else if (cmd == kCmdCopyEmail) {
        SetClipboardText(owner, c->authorEmail);
    }
}

void ShowChangesContextMenu(LogWindowData* d, HWND owner, LPARAM lParam) {
    POINT pt;
    if (!ContextMenuAnchor(d->hChgList, lParam, pt)) return;

    const FileChange* fc = d->selectedChange();
    if (!fc) return;

    const std::wstring path = RepoFilePath(d->params.workTree, fc->path);

    const std::wstring editor = FindEditor();

    int cmd = TrackMenu(owner, pt, {
        {kCmdOpenLocation, L"&Open file location",
         PathExists(ParentDirectory(path))},
        {kCmdEditFile, editor.empty() ? nullptr : L"&Edit file",
         PathExists(path)},
    });

    if (cmd == kCmdOpenLocation && !RevealInExplorer(owner, path)) {
        ShowError(owner, L"Open file location", L"Could not open:\n\n" + path);
    } else if (cmd == kCmdEditFile && !OpenWithEditor(owner, editor, path)) {
        ShowError(owner, L"Edit file", L"Could not open:\n\n" + path);
    }
}

bool OnListKeyDown(LogWindowData* d, HWND hwnd, UINT_PTR listId, WORD vkey) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                      (GetKeyState(VK_MENU)    & 0x8000) == 0;

    if (ctrl && vkey == 'A' && listId == kIdChangesList) {
        SelectAllRows(d->hChgList);
        return true;
    }
    if (vkey == VK_F5) {
        if (listId == kIdCommitList) ReloadCommitList(d, hwnd);
        else                         ReloadSelectedCommit(d);
        return true;
    }
    if (ctrl && vkey == 'C') {
        if (listId == kIdCommitList) {
            const std::wstring sha = d->selectedSha();
            if (!sha.empty()) SetClipboardText(hwnd, sha);
        } else {
            std::wstring paths;
            for (const FileChange* fc : d->selectedChanges()) {
                if (!paths.empty()) paths += L"\r\n";
                paths += fc->path;
            }
            if (!paths.empty()) SetClipboardText(hwnd, paths);
        }
        return true;
    }
    return false;
}

INT_PTR CALLBACK LogDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<LogWindowData>(hwnd);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;

    switch (msg) {
        case WM_INITDIALOG: {
            d = AttachDialogState<LogWindowData>(hwnd, lParam);
            d->hwnd = hwnd;
            if (d->params.loader) {
                d->shown = d->params.loader->AcknowledgeCount();
            }

            CreateChildren(d, hwnd);
            AttachFileMenu(hwnd);
            RefreshTranscript(d);

            d->worker = std::thread(WorkerLoop, d);
            if (d->params.loader) {
                d->params.loader->Notify(hwnd, WM_GITTOOLS_COMMITS);
            }
            if (d->commitCount() > 0) SelectRow(d->hCommitList, 0);

            RECT rc;
            GetClientRect(hwnd, &rc);
            LayoutChildren(d, rc.right, rc.bottom);

            SetFocus(d->hCommitList);
            PostMessageW(hwnd, WM_GITTOOLS_ACTIVATE, 0, 0);
            return FALSE;
        }
        case WM_SIZE:
            if (d) LayoutChildren(d, LOWORD(lParam), HIWORD(lParam));
            return FALSE;
        case WM_TIMER:
            if (d && wParam == kLoadTimerId) {
                KillTimer(hwnd, kLoadTimerId);
                ReloadSelectedCommit(d);
                return TRUE;
            }
            return FALSE;
        case WM_GITTOOLS_ACTIVATE:
            BringDialogToFront(hwnd);
            return TRUE;
        case WM_GITTOOLS_TRANSCRIPT:
            if (d) RefreshTranscript(d);
            return TRUE;
        case WM_GITTOOLS_COMMITS:
            if (d) AppendLoadedCommits(d);
            return TRUE;
        case WM_GITTOOLS_DETAIL: {
            int token = static_cast<int>(wParam);
            auto* details = reinterpret_cast<CommitDetails*>(lParam);
            if (d && token == d->nextToken.load()) {
                ApplyCommitDetails(d, std::move(*details));
                d->loadPending = false;
                RefreshStatus(d);
            }
            delete details;
            return TRUE;
        }
        case WM_CONTEXTMENU: {
            if (!d) return FALSE;
            HWND from = reinterpret_cast<HWND>(wParam);
            if (from == d->hChgList)    ShowChangesContextMenu(d, hwnd, lParam);
            else if (from == d->hCommitList) ShowCommitsContextMenu(d, hwnd, lParam);
            else return FALSE;
            return TRUE;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lParam);
            if (!d) return FALSE;

            if (nm->code == LVN_GETDISPINFO &&
                nm->idFrom == kIdCommitList) {
                auto* di = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                int row = di->item.iItem;
                RequestCommitsNear(d, row);
                if (row >= 0 &&
                    static_cast<size_t>(row) < d->commitCount() &&
                    (di->item.mask & LVIF_TEXT)) {
                    Commit c = d->params.loader->At(static_cast<size_t>(row));
                    switch (di->item.iSubItem) {
                        case 0: d->dispText = std::move(c.subject); break;
                        case 1: d->dispText = std::move(c.author);  break;
                        case 2: d->dispText = std::move(c.date);    break;
                        default: d->dispText.clear();              break;
                    }
                    di->item.pszText = d->dispText.data();
                }
                return FALSE;
            }

            if (nm->code == LVN_GETDISPINFO &&
                nm->idFrom == kIdChangesList) {
                auto* di = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                const int row = di->item.iItem;
                if (row >= 0 &&
                    static_cast<size_t>(row) < d->currentChanges.size() &&
                    (di->item.mask & LVIF_TEXT)) {
                    d->dispText = ChangeCell(d->currentChanges[row],
                                             di->item.iSubItem);
                    di->item.pszText = d->dispText.data();
                }
                return FALSE;
            }

            if (nm->code == LVN_ODFINDITEM &&
                (nm->idFrom == kIdCommitList || nm->idFrom == kIdChangesList)) {
                auto* fi = reinterpret_cast<NMLVFINDITEMW*>(lParam);
                const int found = (nm->idFrom == kIdCommitList)
                                      ? FindCommitByPrefix(d, fi)
                                      : FindChangeByPrefix(d, fi);
                SetWindowLongPtrW(hwnd, DWLP_MSGRESULT,
                                  static_cast<LONG_PTR>(found));
                return TRUE;
            }

            if (nm->code == LVN_KEYDOWN &&
                (nm->idFrom == kIdCommitList || nm->idFrom == kIdChangesList)) {
                auto* kd = reinterpret_cast<NMLVKEYDOWN*>(lParam);
                if (OnListKeyDown(d, hwnd, nm->idFrom, kd->wVKey)) return TRUE;
            }

            if (nm->idFrom == kIdChangesList &&
                nm->code == LVN_ITEMACTIVATE) {
                OpenDiffForSelection(d, hwnd);
                return FALSE;
            }

            if (nm->idFrom == kIdCommitList &&
                nm->code == LVN_ITEMCHANGED) {
                auto* nlv = reinterpret_cast<NMLISTVIEW*>(lParam);
                const bool nowSelected =
                    (nlv->uChanged & LVIF_STATE) &&
                    (nlv->uNewState & LVIS_SELECTED) &&
                    !(nlv->uOldState & LVIS_SELECTED);
                if (nowSelected && nlv->iItem >= 0 &&
                    static_cast<size_t>(nlv->iItem) < d->commitCount()) {
                    ScheduleCommitLoad(d, hwnd);
                }
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (HandleFileMenuCommand(hwnd, wParam)) {
                if (LOWORD(wParam) == kCmdOptions && d && d->params.loader) {
                    d->params.loader->SetUnloadFar(
                        ConfigGetBool(kUnloadFarCommitsKey, false));
                }
                return TRUE;
            }
            if (LOWORD(wParam) == kCmdDebugOutput && d) {
                const bool on = !DebugOutputEnabled();
                SetDebugOutput(on);
                CheckDebugMenu(hwnd, on);
                d->out.SetVisible(hwnd, kIdOutputEdit, on);
                RefreshTranscript(d);
                RECT rc;
                GetClientRect(hwnd, &rc);
                LayoutChildren(d, rc.right, rc.bottom);
                return TRUE;
            }
            if (LOWORD(wParam) == IDOK) {
                if (d && GetFocus() == d->hChgList) {
                    OpenDiffForSelection(d, hwnd);
                }
                return TRUE;
            }
            return FALSE;
        case WM_DESTROY:
            SetTranscriptTarget(nullptr, 0);
            if (d) {
                KillTimer(hwnd, kLoadTimerId);
                d->params.loader.reset();
                {
                    std::lock_guard<std::mutex> lock(d->mu);
                    d->stop = true;
                }
                d->canceller.Cancel();
                d->cv.notify_all();
                if (d->worker.joinable()) d->worker.join();

                MSG pending;
                while (PeekMessageW(&pending, hwnd, WM_GITTOOLS_DETAIL,
                                    WM_GITTOOLS_DETAIL, PM_REMOVE)) {
                    delete reinterpret_cast<CommitDetails*>(pending.lParam);
                }
            }
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

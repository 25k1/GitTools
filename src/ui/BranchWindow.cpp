#include "ui/BranchWindow.hpp"

#include "git/Git.hpp"
#include "ui/AppMenu.hpp"
#include "ui/DialogUtil.hpp"
#include "ui/OutputPane.hpp"
#include "ui/Encoding.hpp"

#include <commctrl.h>

#include <string>
#include <vector>

namespace git_tools {

namespace {

constexpr int  kIdBranchList = 3001;
constexpr int  kIdOutputEdit = 3002;
constexpr int  kIdStatusBar  = 3003;
constexpr UINT WM_GITTOOLS_TRANSCRIPT = WM_APP + 1;
constexpr UINT WM_GITTOOLS_ACTIVATE   = WM_APP + 2;

struct BranchWindowData {
    BranchWindowParams params;
    HWND               hLabel = nullptr;
    HWND               hList  = nullptr;
    OutputPane         out;
};

void PopulateBranchList(HWND hList, const std::vector<Branch>& branches) {
    SendMessageW(hList, LVM_DELETEALLITEMS, 0, 0);
    int row = 0;
    for (const auto& b : branches) {
        AppendRowCells(hList, row++,
                       {b.name, b.isCurrent ? L"*" : L"", b.upstream, b.subject});
    }
}

void FocusCurrentBranch(HWND hList, const std::vector<Branch>& branches) {
    for (size_t i = 0; i < branches.size(); ++i) {
        if (branches[i].isCurrent) {
            SelectRow(hList, static_cast<int>(i));
            ListView_EnsureVisible(hList, static_cast<int>(i), FALSE);
            return;
        }
    }
    if (!branches.empty()) SelectRow(hList, 0);
}

void Reload(BranchWindowData* d) {
    BranchListResult lr = LoadBranchList(d->params.cwd);
    if (!lr.errorMessage.empty()) return;
    d->params.branches = std::move(lr.branches);
    PopulateBranchList(d->hList, d->params.branches);
    FocusCurrentBranch(d->hList, d->params.branches);
}

void OnCheckout(BranchWindowData* d, HWND hwnd) {
    int idx = SelectedIndexIn(d->hList, d->params.branches.size());
    if (idx < 0) return;
    const Branch& b = d->params.branches[idx];
    if (b.isCurrent) return;

    std::wstring target = b.name;
    if (b.isRemote) {
        size_t slash = target.find(L'/');
        if (slash != std::wstring::npos) target = target.substr(slash + 1);
    }
    ProcessResult r = CheckoutBranch(target, d->params.cwd);
    if (!r.started) {
        ShowError(hwnd, L"Checkout failed",
                  L"Failed to launch git:\n\n" + r.errorMessage);
        return;
    }
    if (r.exitCode != 0) {
        std::wstring err = L"git checkout " + b.name + L" failed:\n\n";
        if (!r.stderrText.empty()) err += Utf8ToWide(r.stderrText);
        else if (!r.stdoutText.empty()) err += Utf8ToWide(r.stdoutText);
        ShowError(hwnd, L"Checkout failed", err);
        return;
    }
    Reload(d);
}

void LayoutChildren(BranchWindowData* d, int cx, int cy) {
    const int margin  = 4;
    const int labelH  = 18;
    const int spacing = 4;

    const bool showOut = d->out.visible();
    int availH = cy - d->out.StatusHeight() -
                 (showOut ? 2 * labelH + spacing : labelH) - 2 * margin;
    if (availH < 80) availH = 80;
    int outH  = showOut ? (availH * 30) / 100 : 0;
    int listH = availH - outH;

    StackLayout s{margin, margin, cx - 2 * margin};
    s.Place(d->hLabel, labelH);
    s.Place(d->hList,  listH);
    if (showOut) {
        s.Gap(spacing);
        s.Place(d->out.label, labelH);
        s.Place(d->out.edit,  outH);
    }
}

INT_PTR CALLBACK BranchDlgProc(HWND hwnd, UINT msg,
                               WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<BranchWindowData>(hwnd);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;

    switch (msg) {
        case WM_INITDIALOG: {
            d = AttachDialogState<BranchWindowData>(hwnd, lParam);

            d->hLabel = CreateLabel(hwnd, L"&Branches");
            d->hList  = CreateListView(hwnd, kIdBranchList, LVS_SINGLESEL);
            d->out.Create(hwnd, kIdOutputEdit, kIdStatusBar,
                          WM_GITTOOLS_TRANSCRIPT);
            AttachFileMenu(hwnd);
            d->out.Refresh();

            const Column cols[] = {
                {L"Name",     360},
                {L"State",     50},
                {L"Upstream", 200},
                {L"Subject",  400},
            };
            InsertColumns(d->hList, cols);
            PopulateBranchList(d->hList, d->params.branches);
            FocusCurrentBranch(d->hList, d->params.branches);

            RECT rc;
            GetClientRect(hwnd, &rc);
            LayoutChildren(d, rc.right, rc.bottom);
            SetFocus(d->hList);
            PostMessageW(hwnd, WM_GITTOOLS_ACTIVATE, 0, 0);
            return FALSE;
        }
        case WM_SIZE:
            if (d) LayoutChildren(d, LOWORD(lParam), HIWORD(lParam));
            return FALSE;
        case WM_GITTOOLS_ACTIVATE:
            BringDialogToFront(hwnd);
            return TRUE;
        case WM_GITTOOLS_TRANSCRIPT:
            if (d) d->out.Refresh();
            return TRUE;
        case WM_DESTROY:
            SetTranscriptTarget(nullptr, 0);
            return FALSE;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lParam);
            if (!d) return FALSE;
            if (nm->idFrom == kIdBranchList &&
                nm->code == LVN_ITEMACTIVATE) {
                OnCheckout(d, hwnd);
                return FALSE;
            }
            if (nm->idFrom == kIdBranchList &&
                nm->code == LVN_KEYDOWN) {
                auto* kd = reinterpret_cast<NMLVKEYDOWN*>(lParam);
                if (kd->wVKey == VK_F5) {
                    Reload(d);
                    return TRUE;
                }
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (HandleFileMenuCommand(hwnd, wParam)) return TRUE;
            if (LOWORD(wParam) == kCmdDebugOutput && d) {
                const bool on = !DebugOutputEnabled();
                SetDebugOutput(on);
                CheckDebugMenu(hwnd, on);
                d->out.SetVisible(hwnd, kIdOutputEdit, on);
                d->out.Refresh();
                RECT rc;
                GetClientRect(hwnd, &rc);
                LayoutChildren(d, rc.right, rc.bottom);
                return TRUE;
            }
            if (LOWORD(wParam) == IDOK) {
                if (d && GetFocus() == d->hList) OnCheckout(d, hwnd);
                return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

}

int ShowBranchWindow(const BranchWindowParams& params) {
    BranchWindowData data;
    data.params = params;
    return RunDialog(params.title, 560, 380, nullptr, BranchDlgProc, &data);
}

}

#include "ui/BranchWindow.hpp"

#include "git/Git.hpp"
#include "ui/AppMenu.hpp"
#include "ui/DialogUtil.hpp"
#include "ui/OutputPane.hpp"

#include <commctrl.h>

#include <algorithm>
#include <string>
#include <vector>

namespace git_tools {

namespace {

constexpr int kIdBranchList = 3001;
constexpr int kIdOutputEdit = 3002;
constexpr int kIdStatusBar  = 3003;

struct BranchWindowData {
    BranchWindowParams params;
    HWND               hLabel = nullptr;
    HWND               hList  = nullptr;
    OutputPane         out;
};

void ShowBranches(BranchWindowData* d) {
    const std::vector<Branch>& branches = d->params.branches;
    SendMessageW(d->hList, LVM_DELETEALLITEMS, 0, 0);
    int row = 0;
    for (const Branch& b : branches) {
        AppendRowCells(d->hList, row++,
                       {b.name, b.isCurrent ? L"*" : L"", b.upstream, b.subject});
    }
    if (branches.empty()) return;

    const auto current = std::ranges::find_if(branches, &Branch::isCurrent);
    const int  focus   = current == branches.end()
                             ? 0
                             : static_cast<int>(current - branches.begin());
    SelectRow(d->hList, focus);
    ListView_EnsureVisible(d->hList, focus, FALSE);
}

void Reload(BranchWindowData* d) {
    BranchListResult lr = LoadBranchList(d->params.cwd);
    if (!lr.errorMessage.empty()) return;
    d->params.branches = std::move(lr.branches);
    ShowBranches(d);
}

void OnCheckout(BranchWindowData* d, HWND hwnd) {
    const int idx = SelectedIndexIn(d->hList, d->params.branches.size());
    if (idx < 0) return;
    const Branch& b = d->params.branches[idx];
    if (b.isCurrent) return;

    std::wstring target = b.name;
    if (const size_t slash = target.find(L'/');
        b.isRemote && slash != std::wstring::npos) {
        target.erase(0, slash + 1);
    }
    ProcessResult r = CheckoutBranch(target, d->params.cwd);
    if (!r.started) {
        ShowError(hwnd, L"Checkout failed",
                  L"Failed to launch git:\n\n" + r.errorMessage);
        return;
    }
    if (r.exitCode != 0) {
        ShowError(hwnd, L"Checkout failed",
                  L"git checkout " + b.name + L" failed:\n\n" +
                      Utf8ToWide(r.stderrText.empty() ? r.stdoutText
                                                      : r.stderrText));
        return;
    }
    Reload(d);
}

void LayoutChildren(BranchWindowData* d, HWND hwnd) {
    constexpr int kMargin  = 4;
    constexpr int kLabelH  = 18;
    constexpr int kSpacing = 4;

    RECT rc;
    GetClientRect(hwnd, &rc);
    const int  cx      = rc.right;
    const int  cy      = rc.bottom;
    const bool showOut = d->out.visible();
    const int  availH  = std::max(
        80, cy - d->out.StatusHeight() -
                (showOut ? 2 * kLabelH + kSpacing : kLabelH) - 2 * kMargin);
    const int outH = showOut ? (availH * 30) / 100 : 0;

    StackLayout s{kMargin, kMargin, cx - 2 * kMargin};
    s.Place(d->hLabel, kLabelH);
    s.Place(d->hList,  availH - outH);
    if (showOut) {
        s.Gap(kSpacing);
        s.Place(d->out.label, kLabelH);
        s.Place(d->out.edit,  outH);
    }
}

INT_PTR CALLBACK BranchDlgProc(HWND hwnd, UINT msg,
                               WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<BranchWindowData>(hwnd, msg, lParam);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;

    switch (msg) {
        case WM_INITDIALOG: {
            d->hLabel = CreateLabel(hwnd, L"&Branches");
            d->hList  = CreateListView(hwnd, kIdBranchList, LVS_SINGLESEL);
            d->out.Create(hwnd, kIdOutputEdit, kIdStatusBar);
            AttachFileMenu(hwnd);
            d->out.Refresh();

            const Column cols[] = {
                {L"Name",     360},
                {L"State",     50},
                {L"Upstream", 200},
                {L"Subject",  400},
            };
            InsertColumns(d->hList, cols);
            ShowBranches(d);

            LayoutChildren(d, hwnd);
            SetFocus(d->hList);
            PostMessageW(hwnd, WM_GITTOOLS_ACTIVATE, 0, 0);
            return FALSE;
        }
        case WM_SIZE:
            if (d) LayoutChildren(d, hwnd);
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
            const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
            if (!d || nm->idFrom != kIdBranchList) return FALSE;
            if (nm->code == LVN_ITEMACTIVATE) {
                OnCheckout(d, hwnd);
            } else if (nm->code == LVN_KEYDOWN &&
                       reinterpret_cast<const NMLVKEYDOWN*>(lParam)->wVKey ==
                           VK_F5) {
                Reload(d);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (HandleFileMenuCommand(hwnd, wParam)) return TRUE;
            if (!d) return FALSE;
            if (LOWORD(wParam) == kCmdDebugOutput) {
                ToggleDebugOutput(hwnd, d->out, kIdOutputEdit);
                d->out.Refresh();
                LayoutChildren(d, hwnd);
                return TRUE;
            }
            if (LOWORD(wParam) == IDOK) {
                if (GetFocus() == d->hList) OnCheckout(d, hwnd);
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

#include "ui/FindDialog.hpp"

#include "git/Config.hpp"
#include "ui/DialogUtil.hpp"

namespace git_tools {

namespace {

constexpr int kIdFindEdit   = 4001;
constexpr int kIdMatchCase  = 4002;
constexpr int kIdWrapAround = 4003;

struct FindDialogData {
    FindParams* params = nullptr;
    HWND        hEdit  = nullptr;
    HWND        hCase  = nullptr;
    HWND        hWrap  = nullptr;
};

void CreateControls(FindDialogData* d, HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    constexpr int kMargin = 10;
    constexpr int kGap    = 8;
    constexpr int kLabelW = 74;
    constexpr int kRowH   = 24;
    constexpr int kEditX  = kMargin + kLabelW + kGap;
    constexpr int kCheckY = kMargin + kRowH + kGap;

    CreateChildControl(hwnd, L"STATIC", L"Find &what:", SS_LEFT, 0, -1,
                       kMargin, kMargin + 4, kLabelW, 18);
    d->hEdit = CreateChildControl(hwnd, L"EDIT", L"",
                                  WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                                  kIdFindEdit, kEditX, kMargin,
                                  rc.right - kEditX - kMargin, kRowH);
    d->hCase = CreateChildControl(hwnd, L"BUTTON", L"Match &case",
                                  WS_TABSTOP | BS_AUTOCHECKBOX, 0, kIdMatchCase,
                                  kEditX, kCheckY, 150, 20);
    d->hWrap = CreateChildControl(hwnd, L"BUTTON", L"Wrap &around",
                                  WS_TABSTOP | BS_AUTOCHECKBOX, 0, kIdWrapAround,
                                  kEditX + 160, kCheckY, 150, 20);
    CreateOkCancelButtons(hwnd, kMargin, kGap);
}

INT_PTR CALLBACK FindDlgProc(HWND hwnd, UINT msg,
                             WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<FindDialogData>(hwnd, msg, lParam);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;

    switch (msg) {
        case WM_INITDIALOG:
            CreateControls(d, hwnd);
            ApplyDialogFont(hwnd);
            SetWindowTextW(d->hEdit, d->params->what.c_str());
            SetChecked(d->hCase, d->params->matchCase);
            SetChecked(d->hWrap, d->params->wrapAround);
            SetFocus(d->hEdit);
            SendMessageW(d->hEdit, EM_SETSEL, 0, -1);
            return FALSE;
        case WM_COMMAND: {
            if (LOWORD(wParam) != IDOK) return FALSE;
            std::wstring what = ControlText(d->hEdit);
            if (what.empty()) {
                EndDialog(hwnd, 0);
                return TRUE;
            }
            d->params->what      = std::move(what);
            d->params->matchCase = IsChecked(d->hCase);
            if (const bool wrap = IsChecked(d->hWrap);
                wrap != d->params->wrapAround) {
                d->params->wrapAround = wrap;
                ConfigSetBool(kWrapAroundKey, wrap);
            }
            EndDialog(hwnd, 1);
            return TRUE;
        }
    }
    return FALSE;
}

}

bool ShowFindDialog(HWND owner, FindParams& params) {
    FindDialogData data;
    data.params = &params;
    return RunDialogEx(L"Find", 300, 70, owner, FindDlgProc, &data, false) == 1;
}

}

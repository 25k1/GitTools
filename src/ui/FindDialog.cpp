#include "ui/FindDialog.hpp"

#include "git/Config.hpp"
#include "ui/DialogUtil.hpp"

#include <commctrl.h>

namespace git_tools {

namespace {

constexpr int kIdFindEdit  = 4001;
constexpr int kIdMatchCase = 4002;
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

    const int margin = 10;
    const int gap    = 8;
    const int labelW = 74;
    const int rowH   = 24;
    const int editX  = margin + labelW + gap;
    const int editW  = rc.right - editX - margin;

    CreateChildControl(hwnd, L"STATIC", L"Find &what:", SS_LEFT, 0, -1,
                margin, margin + 4, labelW, 18);

    d->hEdit = CreateChildControl(hwnd, L"EDIT", L"",
                           WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                           kIdFindEdit, editX, margin, editW, rowH);

    d->hCase = CreateChildControl(hwnd, L"BUTTON", L"Match &case",
                           WS_TABSTOP | BS_AUTOCHECKBOX, 0, kIdMatchCase,
                           editX, margin + rowH + gap, 150, 20);

    d->hWrap = CreateChildControl(hwnd, L"BUTTON", L"Wrap &around",
                           WS_TABSTOP | BS_AUTOCHECKBOX, 0, kIdWrapAround,
                           editX + 160, margin + rowH + gap, 150, 20);

    const int btnW = 82;
    const int btnH = 26;
    const int btnY = rc.bottom - margin - btnH;
    CreateChildControl(hwnd, L"BUTTON", L"OK",
                WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK,
                rc.right - margin - 2 * btnW - gap, btnY, btnW, btnH);
    CreateChildControl(hwnd, L"BUTTON", L"Cancel",
                WS_TABSTOP | BS_PUSHBUTTON, 0, IDCANCEL,
                rc.right - margin - btnW, btnY, btnW, btnH);
}

INT_PTR CALLBACK FindDlgProc(HWND hwnd, UINT msg,
                             WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<FindDialogData>(hwnd);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;

    switch (msg) {
        case WM_INITDIALOG: {
            d = AttachDialogState<FindDialogData>(hwnd, lParam);

            CreateControls(d, hwnd);
            ApplyDialogFont(hwnd);

            SetWindowTextW(d->hEdit, d->params->what.c_str());
            SendMessageW(d->hCase, BM_SETCHECK,
                         d->params->matchCase ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(d->hWrap, BM_SETCHECK,
                         d->params->wrapAround ? BST_CHECKED : BST_UNCHECKED, 0);

            SetFocus(d->hEdit);
            SendMessageW(d->hEdit, EM_SETSEL, 0, -1);
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                std::wstring what = ControlText(d->hEdit);
                if (what.empty()) {
                    EndDialog(hwnd, 0);
                    return TRUE;
                }
                d->params->what = std::move(what);
                d->params->matchCase =
                    SendMessageW(d->hCase, BM_GETCHECK, 0, 0) == BST_CHECKED;

                const bool wrap =
                    SendMessageW(d->hWrap, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (wrap != d->params->wrapAround) {
                    d->params->wrapAround = wrap;
                    ConfigSetBool(L"wraparound", wrap);
                }
                EndDialog(hwnd, 1);
                return TRUE;
            }
            return FALSE;
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

#include "ui/OptionsDialog.hpp"

#include "git/Config.hpp"
#include "ui/DialogUtil.hpp"
#include "ui/DiffWindow.hpp"
#include "ui/Shell.hpp"

#include <string>

namespace git_tools {

namespace {

constexpr int kIdEditorCommand = 4101;
constexpr int kIdVolume        = 4102;
constexpr int kIdVolumeValue   = 4103;

struct OptionsDialogData {
    HWND hCommand     = nullptr;
    HWND hVolume      = nullptr;
    HWND hVolumeValue = nullptr;
};

int VolumePos(OptionsDialogData* d) {
    return static_cast<int>(SendMessageW(d->hVolume, TBM_GETPOS, 0, 0));
}

void UpdateVolumeLabel(OptionsDialogData* d) {
    SetWindowTextW(d->hVolumeValue, std::to_wstring(VolumePos(d)).c_str());
}

void CreateControls(OptionsDialogData* d, HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    const int margin = 10;
    const int gap    = 8;
    const int labelW = 84;
    const int rowH   = 24;
    const int fieldX = margin + labelW + gap;
    const int fieldW = rc.right - fieldX - margin;

    CreateChildControl(hwnd, L"STATIC", L"&Editor path:", SS_LEFT, 0, -1,
                       margin, margin + 4, labelW, 18);
    d->hCommand = CreateChildControl(
        hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
        kIdEditorCommand, fieldX, margin, fieldW, rowH);

    CreateChildControl(
        hwnd, L"STATIC",
        L"Editor for Edit file: path, optionally followed by arguments. "
        L"Quote the path if it contains spaces. %1 is replaced by the file "
        L"path (appended if absent), %L by the line number. Notepad++ gets "
        L"-n<line> automatically.",
        SS_LEFT, 0, -1,
        margin, margin + rowH + gap, rc.right - 2 * margin, 46);

    const int btnW = 82;
    const int btnH = 26;
    const int btnY = rc.bottom - margin - btnH;

    const int volH = 30;
    const int volY = btnY - gap - volH;
    const int valW = 40;

    CreateChildControl(hwnd, L"STATIC", L"&Volume:", SS_LEFT, 0, -1,
                       margin, volY + 6, labelW, 18);
    d->hVolume = CreateChildControl(
        hwnd, TRACKBAR_CLASSW, L"",
        WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 0, kIdVolume,
        fieldX, volY, fieldW - valW - gap, volH);
    d->hVolumeValue = CreateChildControl(
        hwnd, L"STATIC", L"", SS_RIGHT, 0, kIdVolumeValue,
        rc.right - margin - valW, volY + 6, valW, 18);

    CreateChildControl(hwnd, L"BUTTON", L"OK",
                       WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK,
                       rc.right - margin - 2 * btnW - gap, btnY, btnW, btnH);
    CreateChildControl(hwnd, L"BUTTON", L"Cancel",
                       WS_TABSTOP | BS_PUSHBUTTON, 0, IDCANCEL,
                       rc.right - margin - btnW, btnY, btnW, btnH);
}

INT_PTR CALLBACK OptionsDlgProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<OptionsDialogData>(hwnd);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;

    switch (msg) {
        case WM_INITDIALOG: {
            d = AttachDialogState<OptionsDialogData>(hwnd, lParam);
            CreateControls(d, hwnd);
            ApplyDialogFont(hwnd);

            SetWindowTextW(d->hCommand, ConfigGet(L"editor").c_str());

            int volume = ConfigGetInt(L"soundvolume", 50);
            if (volume < 0)   volume = 0;
            if (volume > 100) volume = 100;
            SendMessageW(d->hVolume, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(d->hVolume, TBM_SETTICFREQ, 10, 0);
            SendMessageW(d->hVolume, TBM_SETPAGESIZE, 0, 10);
            SendMessageW(d->hVolume, TBM_SETPOS, TRUE, volume);
            UpdateVolumeLabel(d);

            SetFocus(d->hCommand);
            SendMessageW(d->hCommand, EM_SETSEL, 0, -1);
            return FALSE;
        }
        case WM_HSCROLL:
            if (d && reinterpret_cast<HWND>(lParam) == d->hVolume) {
                UpdateVolumeLabel(d);
                return TRUE;
            }
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                ConfigSet(L"editor", ControlText(d->hCommand));
                ConfigSet(L"soundvolume", std::to_wstring(VolumePos(d)));
                ResetEditorCache();
                ResetSoundCache();
                EndDialog(hwnd, 1);
                return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

}

bool ShowOptionsDialog(HWND owner) {
    OptionsDialogData data;
    return RunDialogEx(L"Options", 340, 130, owner, OptionsDlgProc,
                       &data, false) == 1;
}

}

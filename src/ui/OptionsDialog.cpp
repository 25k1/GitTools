#include "ui/OptionsDialog.hpp"

#include "audio/Audio.hpp"
#include "git/Config.hpp"
#include "ui/DialogUtil.hpp"
#include "ui/Shell.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace git_tools {

namespace {

constexpr int kIdEditorCommand = 4101;
constexpr int kIdVolume        = 4102;
constexpr int kIdVolumeValue   = 4103;
constexpr int kIdUnloadFar     = 4104;
constexpr int kIdAudioDevice   = 4105;

struct OptionsDialogData {
    HWND                     hCommand     = nullptr;
    HWND                     hVolume      = nullptr;
    HWND                     hVolumeValue = nullptr;
    HWND                     hUnloadFar   = nullptr;
    HWND                     hDevice      = nullptr;
    std::vector<AudioDevice> devices;
};

int VolumePos(const OptionsDialogData* d) {
    return static_cast<int>(SendMessageW(d->hVolume, TBM_GETPOS, 0, 0));
}

void UpdateVolumeLabel(const OptionsDialogData* d) {
    SetWindowTextW(d->hVolumeValue, std::to_wstring(VolumePos(d)).c_str());
}

void CreateControls(OptionsDialogData* d, HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    constexpr int kMargin = 10;
    constexpr int kGap    = 8;
    constexpr int kLabelW = 84;
    constexpr int kRowH   = 24;
    constexpr int kFieldX = kMargin + kLabelW + kGap;
    constexpr int kVolH   = 30;
    constexpr int kValueW = 40;
    constexpr int kCheckH = 20;
    const int     width   = rc.right;
    const int     fieldW  = width - kFieldX - kMargin;
    const int     volY    =
        rc.bottom - kMargin - kButtonHeight - kGap - kVolH;
    const int     devY    = volY - kGap - kRowH;

    CreateChildControl(hwnd, L"STATIC", L"&Editor path:", SS_LEFT, 0, -1,
                       kMargin, kMargin + 4, kLabelW, 18);
    d->hCommand = CreateChildControl(
        hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
        kIdEditorCommand, kFieldX, kMargin, fieldW, kRowH);
    CreateChildControl(
        hwnd, L"STATIC",
        L"Editor for Edit file: path, optionally followed by arguments. "
        L"Quote the path if it contains spaces. %1 is replaced by the file "
        L"path (appended if absent), %L by the line number. Notepad++ gets "
        L"-n<line> automatically.",
        SS_LEFT, 0, -1,
        kMargin, kMargin + kRowH + kGap, width - 2 * kMargin, 46);

    d->hUnloadFar = CreateChildControl(
        hwnd, L"BUTTON",
        L"&Unload commits far from view to save memory "
        L"(reloaded from git when needed)",
        WS_TABSTOP | BS_AUTOCHECKBOX, 0, kIdUnloadFar,
        kMargin, devY - kGap - kCheckH, width - 2 * kMargin, kCheckH);

    CreateChildControl(hwnd, L"STATIC", L"Output &device:", SS_LEFT, 0, -1,
                       kMargin, devY + 4, kLabelW, 18);
    d->hDevice = CreateChildControl(
        hwnd, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0,
        kIdAudioDevice, kFieldX, devY, fieldW, 200);

    CreateChildControl(hwnd, L"STATIC", L"&Volume:", SS_LEFT, 0, -1,
                       kMargin, volY + 6, kLabelW, 18);
    d->hVolume = CreateChildControl(
        hwnd, TRACKBAR_CLASSW, L"",
        WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 0, kIdVolume,
        kFieldX, volY, fieldW - kValueW - kGap, kVolH);
    d->hVolumeValue = CreateChildControl(
        hwnd, L"STATIC", L"", SS_RIGHT, 0, kIdVolumeValue,
        width - kMargin - kValueW, volY + 6, kValueW, 18);

    CreateOkCancelButtons(hwnd, kMargin, kGap);
}

void FillAudioDevices(OptionsDialogData* d) {
    const std::wstring saved = ConfigGet(kAudioDeviceKey);
    d->devices = ListAudioDevices();
    d->devices.insert(d->devices.begin(), AudioDevice{L"", L"Default device"});
    auto selected = std::ranges::find(d->devices, saved, &AudioDevice::id);
    if (selected == d->devices.end()) {
        d->devices.push_back(AudioDevice{saved, L"Unavailable device"});
        selected = d->devices.end() - 1;
    }
    for (const AudioDevice& device : d->devices) {
        SendMessageW(d->hDevice, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(device.name.c_str()));
    }
    SendMessageW(d->hDevice, CB_SETCURSEL, selected - d->devices.begin(), 0);
}

std::wstring SelectedAudioDevice(const OptionsDialogData* d) {
    const LRESULT i = SendMessageW(d->hDevice, CB_GETCURSEL, 0, 0);
    return (i >= 0 && static_cast<size_t>(i) < d->devices.size())
               ? d->devices[i].id
               : std::wstring();
}

INT_PTR CALLBACK OptionsDlgProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<OptionsDialogData>(hwnd, msg, lParam);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;

    switch (msg) {
        case WM_INITDIALOG:
            CreateControls(d, hwnd);
            ApplyDialogFont(hwnd);

            SetWindowTextW(d->hCommand, ConfigGet(kEditorKey).c_str());
            SendMessageW(d->hVolume, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(d->hVolume, TBM_SETTICFREQ, 10, 0);
            SendMessageW(d->hVolume, TBM_SETPAGESIZE, 0, 10);
            SendMessageW(d->hVolume, TBM_SETPOS, TRUE, SoundVolumePercent());
            UpdateVolumeLabel(d);
            SetChecked(d->hUnloadFar, ConfigGetBool(kUnloadFarCommitsKey, false));
            FillAudioDevices(d);

            SetFocus(d->hCommand);
            SendMessageW(d->hCommand, EM_SETSEL, 0, -1);
            return FALSE;
        case WM_HSCROLL:
            if (d && reinterpret_cast<HWND>(lParam) == d->hVolume) {
                UpdateVolumeLabel(d);
                return TRUE;
            }
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wParam) != IDOK) return FALSE;
            ConfigSet(kEditorKey, ControlText(d->hCommand));
            ConfigSet(kSoundVolumeKey, std::to_wstring(VolumePos(d)));
            ConfigSetBool(kUnloadFarCommitsKey, IsChecked(d->hUnloadFar));
            ConfigSet(kAudioDeviceKey, SelectedAudioDevice(d));
            ResetEditorCache();
            CloseAudio();
            EndDialog(hwnd, 1);
            return TRUE;
    }
    return FALSE;
}

}

bool ShowOptionsDialog(HWND owner) {
    OptionsDialogData data;
    return RunDialogEx(L"Options", 340, 170, owner, OptionsDlgProc,
                       &data, false) == 1;
}

}

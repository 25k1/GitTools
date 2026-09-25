#include "audio/Audio.hpp"
#include "git/Config.hpp"
#include "ui/Shell.hpp"

#include "ui/OptionsDialog.hpp"

#include "ui/ColumnsDialog.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <string>
#include <vector>

namespace git_tools {

namespace {

std::vector<AudioDevice> AudioDeviceChoices(size_t& selected) {
    const std::wstring saved = ConfigGet(kAudioDeviceKey);
    std::vector<AudioDevice> devices = ListAudioDevices();
    devices.insert(devices.begin(), AudioDevice{L"", L"Default device"});
    auto match = std::ranges::find(devices, saved, &AudioDevice::id);
    if (match == devices.end()) {
        devices.push_back(AudioDevice{saved, L"Unavailable device"});
        match = devices.end() - 1;
    }
    selected = static_cast<size_t>(match - devices.begin());
    return devices;
}

}

bool ShowOptionsDialog(wxWindow* owner) {
    wxDialog dialog(owner, wxID_ANY, L"Options");
    const int gap = dialog.FromDIP(8);

    auto* editorLabel = new wxStaticText(&dialog, wxID_ANY, L"&Editor path:");
    auto* editor = new wxTextCtrl(&dialog, wxID_ANY, ConfigGet(kEditorKey));
    auto* editorHelp = new wxStaticText(
        &dialog, wxID_ANY,
        L"Editor for Edit file: path, optionally followed by arguments. "
        L"Quote the path if it contains spaces. %1 is replaced by the file "
        L"path (appended if absent), %L by the line number. Notepad++ gets "
        L"-n<line> automatically.");

    auto* unloadFar = new wxCheckBox(
        &dialog, wxID_ANY,
        L"&Unload commits far from view to save memory "
        L"(reloaded from git when needed)");
    unloadFar->SetValue(ConfigGetBool(kUnloadFarCommitsKey, false));

    auto* diffMarkers = new wxCheckBox(
        &dialog, wxID_ANY,
        L"Show + and - &indicators in the diff viewer (Ctrl+I)");
    diffMarkers->SetValue(ConfigGetBool(kDiffMarkersKey, true));

    bool columnsChanged = false;
    auto* columns = new wxButton(&dialog, wxID_ANY, L"Configure &columns...");
    columns->Bind(wxEVT_BUTTON, [&dialog, &columnsChanged](wxCommandEvent&) {
        if (ShowColumnsDialog(&dialog)) columnsChanged = true;
    });

    size_t selectedDevice = 0;
    const std::vector<AudioDevice> devices = AudioDeviceChoices(selectedDevice);
    auto* deviceLabel = new wxStaticText(&dialog, wxID_ANY, L"Output &device:");
    auto* device = new wxChoice(&dialog, wxID_ANY);
    for (const AudioDevice& d : devices) device->Append(d.name);
    device->SetSelection(static_cast<int>(selectedDevice));

    auto* volumeLabel = new wxStaticText(&dialog, wxID_ANY, L"&Volume:");
    auto* volume = new wxSlider(&dialog, wxID_ANY, SoundVolumePercent(), 0, 100,
                                wxDefaultPosition, wxDefaultSize,
                                wxSL_HORIZONTAL | wxSL_AUTOTICKS);
    volume->SetTickFreq(10);
    volume->SetPageSize(10);
    auto* volumeValue = new wxStaticText(
        &dialog, wxID_ANY, std::to_wstring(volume->GetValue()),
        wxDefaultPosition, wxSize(dialog.FromDIP(40), -1),
        wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
    volume->Bind(wxEVT_SLIDER, [volume, volumeValue](wxCommandEvent&) {
        volumeValue->SetLabel(std::to_wstring(volume->GetValue()));
    });

    auto* grid = new wxFlexGridSizer(2, gap / 2, gap);
    grid->AddGrowableCol(1);
    grid->Add(editorLabel, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(editor, 1, wxEXPAND);
    grid->AddSpacer(0);
    grid->Add(editorHelp, 0, wxEXPAND);

    auto* volumeRow = new wxBoxSizer(wxHORIZONTAL);
    volumeRow->Add(volume, 1, wxALIGN_CENTER_VERTICAL);
    volumeRow->Add(volumeValue, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);

    auto* audio = new wxFlexGridSizer(2, gap / 2, gap);
    audio->AddGrowableCol(1);
    audio->Add(deviceLabel, 0, wxALIGN_CENTER_VERTICAL);
    audio->Add(device, 1, wxEXPAND);
    audio->Add(volumeLabel, 0, wxALIGN_CENTER_VERTICAL);
    audio->Add(volumeRow, 1, wxEXPAND);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 0, wxEXPAND | wxALL, gap);
    sizer->Add(unloadFar, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
    sizer->Add(diffMarkers, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
    sizer->Add(columns, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
    sizer->Add(audio, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, gap);
    sizer->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0,
               wxEXPAND | wxALL, gap);
    dialog.SetSizer(sizer);
    editor->SetMinSize(wxSize(dialog.FromDIP(420), -1));
    editorHelp->Wrap(dialog.FromDIP(420));
    sizer->Fit(&dialog);
    dialog.CentreOnParent();

    editor->SetFocus();
    editor->SelectAll();

    if (dialog.ShowModal() != wxID_OK) return columnsChanged;

    const int choice = device->GetSelection();
    ConfigSet(kEditorKey, editor->GetValue().ToStdWstring());
    ConfigSet(kSoundVolumeKey, std::to_wstring(volume->GetValue()));
    ConfigSetBool(kUnloadFarCommitsKey, unloadFar->GetValue());
    ConfigSetBool(kDiffMarkersKey, diffMarkers->GetValue());
    ConfigSet(kAudioDeviceKey,
              choice >= 0 && static_cast<size_t>(choice) < devices.size()
                  ? devices[static_cast<size_t>(choice)].id
                  : std::wstring());
    ResetEditorCache();
    CloseAudio();
    return true;
}

}

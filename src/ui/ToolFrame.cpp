#include "git/Config.hpp"
#include "git/Transcript.hpp"

#include "ui/ToolFrame.hpp"

#include "ui/Widgets.hpp"

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace git_tools {

namespace {

bool& DebugOutputFlag() {
    static bool on = ConfigGetBool(kDebugOutputKey, false);
    return on;
}

}

ToolFrame::ToolFrame(const std::wstring& title, const wxSize& size)
    : MenuFrame(nullptr, title, size) {
    panel_ = new wxPanel(this);
    sizer_ = new wxBoxSizer(wxVERTICAL);
    CreateStatusBar();

    SetTranscriptListener([this] { CallAfter([this] { RefreshTranscript(); }); });
}

ToolFrame::~ToolFrame() {
    SetTranscriptListener(nullptr);
}

wxStaticText* ToolFrame::AddLabel(const wchar_t* text) {
    auto* label = new wxStaticText(panel_, wxID_ANY, text);
    sizer_->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
    return label;
}

void ToolFrame::AddPane(wxWindow* pane, int proportion) {
    sizer_->Add(pane, proportion, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));
}

void ToolFrame::FinishLayout(wxWindow* absorber, int outputShare) {
    outputLabel_ = AddLabel(L"&Output");
    output_      = CreateReadOnlyText(panel_, wxTE_DONTWRAP | wxHSCROLL);
    AddPane(output_, outputShare);
    sizer_->AddSpacer(FromDIP(4));
    panel_->SetSizer(sizer_);

    BuildMenus();
    absorber_     = absorber;
    absorberBase_ = sizer_->GetItem(absorber)->GetProportion();
    outputShare_  = outputShare;
    ApplyOutputVisibility();
    RefreshTranscript();
}

std::vector<MenuSection> ToolFrame::Menus() {
    return {{L"&File", {
        {L"&Debug output", [this] { ToggleDebugOutput(); }, true, true, DebugOutputFlag()},
        OptionsEntry(),
        kMenuSeparator,
        CloseEntry(L"E&xit"),
    }}};
}

std::wstring ToolFrame::StatusText() const {
    return TranscriptStatus();
}

void ToolFrame::RefreshStatus() {
    SetStatusText(StatusText());
}

void ToolFrame::RefreshTranscript() {
    if (output_ && output_->IsShown()) {
        const TranscriptChunk chunk = TranscriptSince(cursor_);
        if (chunk.reset) {
            output_->ChangeValue(chunk.text);
            MoveCaret(output_, output_->GetLastPosition());
        } else if (!chunk.text.empty()) {
            output_->AppendText(chunk.text);
        }
    }
    RefreshStatus();
}

void ToolFrame::ToggleDebugOutput() {
    const bool on = !DebugOutputFlag();
    DebugOutputFlag() = on;
    ConfigSetBool(kDebugOutputKey, on);
    ApplyOutputVisibility();
    RefreshTranscript();
}

void ToolFrame::ApplyOutputVisibility() {
    const bool on = DebugOutputFlag();
    if (on && !output_->IsShown()) {
        output_->ChangeValue(wxString());
        cursor_ = 0;
    }
    sizer_->Show(outputLabel_, on);
    sizer_->Show(output_, on);
    sizer_->GetItem(absorber_)->SetProportion(absorberBase_ +
                                              (on ? 0 : outputShare_));
    panel_->Layout();
}

}

#include "git/Config.hpp"
#include "git/Transcript.hpp"

#include "ui/ToolFrame.hpp"

#include "ui/OptionsDialog.hpp"
#include "ui/Widgets.hpp"

#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace git_tools {

namespace {

constexpr int kIdDebugOutput = wxID_HIGHEST + 1;
constexpr int kIdOptions     = wxID_HIGHEST + 2;

bool& DebugOutputFlag() {
    static bool on = ConfigGetBool(kDebugOutputKey, false);
    return on;
}

}

ToolFrame::ToolFrame(const std::wstring& title, const wxSize& size)
    : wxFrame(nullptr, wxID_ANY, title) {
    SetSize(FromDIP(size));
    panel_ = new wxPanel(this);
    sizer_ = new wxBoxSizer(wxVERTICAL);

    auto* file = new wxMenu;
    file->AppendCheckItem(kIdDebugOutput, L"&Debug output");
    file->Check(kIdDebugOutput, DebugOutputFlag());
    file->Append(kIdOptions, L"&Options...");
    file->AppendSeparator();
    file->Append(wxID_EXIT, L"E&xit");
    auto* bar = new wxMenuBar;
    bar->Append(file, L"&File");
    SetMenuBar(bar);
    CreateStatusBar();

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ToggleDebugOutput(); },
         kIdDebugOutput);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (ShowOptionsDialog(this)) OnOptionsChanged();
    }, kIdOptions);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    CloseOnEscape(this, [this] { Close(); });

    SetTranscriptListener([this] { CallAfter([this] { RefreshTranscript(); }); });
}

ToolFrame::~ToolFrame() {
    SetTranscriptListener(nullptr);
}

void ToolFrame::AddLabel(const wchar_t* text) {
    sizer_->Add(new wxStaticText(panel_, wxID_ANY, text), 0,
                wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
}

void ToolFrame::AddPane(wxWindow* pane, int proportion) {
    sizer_->Add(pane, proportion, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));
}

void ToolFrame::FinishLayout(wxWindow* absorber, int outputShare) {
    outputLabel_ = new wxStaticText(panel_, wxID_ANY, L"&Output");
    sizer_->Add(outputLabel_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
    output_ = CreateReadOnlyText(panel_, wxTE_DONTWRAP | wxHSCROLL);
    AddPane(output_, outputShare);
    sizer_->AddSpacer(FromDIP(4));
    panel_->SetSizer(sizer_);

    absorber_     = absorber;
    absorberBase_ = sizer_->GetItem(absorber)->GetProportion();
    outputShare_  = outputShare;
    ApplyOutputVisibility();
    RefreshTranscript();
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
    GetMenuBar()->Check(kIdDebugOutput, on);
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

#include "git/Config.hpp"

#include "ui/FindDialog.hpp"

#include "ui/Widgets.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace git_tools {

bool ShowFindDialog(wxWindow* owner, FindParams& params) {
    wxDialog dialog(owner, wxID_ANY, L"Find");

    auto* label      = new wxStaticText(&dialog, wxID_ANY, L"Find &what:");
    auto* what       = new wxTextCtrl(&dialog, wxID_ANY, params.what);
    auto* matchCase  = new wxCheckBox(&dialog, wxID_ANY, L"Match &case");
    auto* wrapAround = new wxCheckBox(&dialog, wxID_ANY, L"Wrap &around");
    matchCase->SetValue(params.matchCase);
    wrapAround->SetValue(params.wrapAround);

    const int gap = dialog.FromDIP(8);

    auto* checks = new wxBoxSizer(wxHORIZONTAL);
    checks->Add(matchCase, 0, wxRIGHT, gap);
    checks->Add(wrapAround);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(LabeledRow(label, what, gap), 0, wxEXPAND | wxALL, gap);
    sizer->Add(checks, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
    what->SetMinSize(wxSize(dialog.FromDIP(320), -1));
    FinishDialog(dialog, sizer, gap);

    what->SetFocus();
    what->SelectAll();

    if (dialog.ShowModal() != wxID_OK) return false;

    std::wstring text = what->GetValue().ToStdWstring();
    if (text.empty()) return false;
    params.what      = std::move(text);
    params.matchCase = matchCase->GetValue();
    if (const bool wrap = wrapAround->GetValue(); wrap != params.wrapAround) {
        params.wrapAround = wrap;
        ConfigSetBool(kWrapAroundKey, wrap);
    }
    return true;
}

}

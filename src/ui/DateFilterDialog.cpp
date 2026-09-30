#include "ui/DateFilterDialog.hpp"

#include "ui/App.hpp"
#include "ui/Widgets.hpp"

#include <wx/checkbox.h>
#include <wx/datectrl.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <string>

namespace git_tools {

namespace {

constexpr wchar_t kTitle[]         = L"Filter by date";
constexpr int     kDefaultSpanDays = 30;

wxDateTime ParseDate(const std::wstring& text, const wxDateTime& fallback) {
    wxDateTime date;
    return !text.empty() && date.ParseISODate(text) ? date : fallback;
}

struct DateBound {
    wxCheckBox*       enabled = nullptr;
    wxDatePickerCtrl* picker  = nullptr;

    bool IsSet() const { return enabled->GetValue(); }

    std::wstring Value() const {
        return IsSet() ? picker->GetValue().FormatISODate().ToStdWstring() : std::wstring();
    }
};

DateBound AddBound(wxDialog& dialog, wxSizer* sizer, const wchar_t* check,
                   const wchar_t* label, const std::wstring& value,
                   const wxDateTime& fallback, int gap) {
    DateBound bound;
    bound.enabled = CreateCheckBox(&dialog, check, !value.empty());
    auto* caption = new wxStaticText(&dialog, wxID_ANY, label);
    bound.picker  = new wxDatePickerCtrl(&dialog, wxID_ANY, ParseDate(value, fallback),
                                         wxDefaultPosition, wxDefaultSize,
                                         wxDP_DROPDOWN | wxDP_SHOWCENTURY);
    bound.picker->Enable(bound.IsSet());
    bound.enabled->Bind(wxEVT_CHECKBOX, [picker = bound.picker](wxCommandEvent& event) {
        picker->Enable(event.IsChecked());
    });

    sizer->Add(bound.enabled, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    sizer->Add(LabeledRow(caption, bound.picker, gap), 0,
               wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    return bound;
}

}

bool ShowDateFilterDialog(wxWindow* owner, DateRange& range) {
    wxDialog dialog(owner, wxID_ANY, kTitle);
    const int        gap   = dialog.FromDIP(8);
    const wxDateTime today = wxDateTime::Today();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    const DateBound since = AddBound(dialog, sizer, L"Filter by &start date", L"Start date:",
                                     range.since, today - wxDateSpan::Days(kDefaultSpanDays),
                                     gap);
    const DateBound until = AddBound(dialog, sizer, L"Filter by &end date", L"End date:",
                                     range.until, today, gap);
    FinishDialog(dialog, sizer, gap);

    dialog.Bind(wxEVT_BUTTON, [&](wxCommandEvent& event) {
        if (since.IsSet() && until.IsSet() &&
            since.picker->GetValue() > until.picker->GetValue()) {
            ShowError(&dialog, kTitle, L"The start date must not be later than the end date.");
            since.picker->SetFocus();
            return;
        }
        event.Skip();
    }, wxID_OK);
    since.enabled->SetFocus();

    if (dialog.ShowModal() != wxID_OK) return false;
    range = {since.Value(), until.Value()};
    return true;
}

}

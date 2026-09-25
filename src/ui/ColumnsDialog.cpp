#include "ui/ColumnsDialog.hpp"

#include "ui/App.hpp"
#include "ui/Columns.hpp"
#include "ui/Widgets.hpp"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace git_tools {

namespace {

enum class Shift { Up, Down, Top, Bottom };

class ColumnsDialog : public wxDialog {
public:
    explicit ColumnsDialog(wxWindow* owner);

    bool SaveChanges();

private:
    void ShowSet(size_t index);
    void CaptureChecks();
    void RefreshRows(size_t from, size_t to);
    void MoveSelected(Shift shift);
    bool ListHasFocus() const;
    void OnOk(wxCommandEvent& event);
    void OnCharHook(wxKeyEvent& event);

    std::span<const ColumnSet* const> sets_;
    std::vector<ColumnLayout>         saved_;
    std::vector<ColumnLayout>         layouts_;
    size_t                            current_ = 0;
    wxChoice*                         choice_  = nullptr;
    wxListView*                       list_    = nullptr;
};

ColumnsDialog::ColumnsDialog(wxWindow* owner)
    : wxDialog(owner, wxID_ANY, L"Configure columns"), sets_(ColumnSets()) {
    for (const ColumnSet* set : sets_) saved_.push_back(LoadColumnLayout(*set));
    layouts_ = saved_;

    const int gap = FromDIP(8);

    auto* choiceLabel = new wxStaticText(this, wxID_ANY, L"&List:");
    choice_ = new wxChoice(this, wxID_ANY);
    for (const ColumnSet* set : sets_) choice_->Append(set->title);

    auto* listLabel = new wxStaticText(this, wxID_ANY, L"&Columns:");
    list_ = new wxListView(this, wxID_ANY, wxDefaultPosition,
                           FromDIP(wxSize(260, 220)),
                           wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    list_->EnableCheckBoxes();
    list_->AppendColumn(L"Column");

    auto* up     = new wxButton(this, wxID_ANY, L"Move &up");
    auto* down   = new wxButton(this, wxID_ANY, L"Move &down");
    auto* top    = new wxButton(this, wxID_ANY, L"Move to &top");
    auto* bottom = new wxButton(this, wxID_ANY, L"Move to &bottom");

    auto* choiceRow = new wxBoxSizer(wxHORIZONTAL);
    choiceRow->Add(choiceLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    choiceRow->Add(choice_, 1, wxEXPAND);

    auto* buttons = new wxBoxSizer(wxVERTICAL);
    for (wxButton* button : {up, down, top, bottom}) {
        buttons->Add(button, 0, wxEXPAND | wxBOTTOM, gap / 2);
    }

    auto* listRow = new wxBoxSizer(wxHORIZONTAL);
    listRow->Add(list_, 1, wxEXPAND);
    listRow->Add(buttons, 0, wxLEFT, gap);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(choiceRow, 0, wxEXPAND | wxALL, gap);
    sizer->Add(listLabel, 0, wxLEFT | wxRIGHT, gap);
    sizer->Add(listRow, 1, wxEXPAND | wxALL, gap);
    sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0,
               wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, gap);
    SetSizerAndFit(sizer);
    CentreOnParent();

    list_->SetColumnWidth(0, list_->GetClientSize().x);
    list_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        list_->SetColumnWidth(0, list_->GetClientSize().x);
    });

    choice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const int index = choice_->GetSelection();
        if (index < 0) return;
        CaptureChecks();
        ShowSet(static_cast<size_t>(index));
    });
    up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveSelected(Shift::Up); });
    down->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveSelected(Shift::Down); });
    top->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveSelected(Shift::Top); });
    bottom->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveSelected(Shift::Bottom); });
    Bind(wxEVT_BUTTON, &ColumnsDialog::OnOk, this, wxID_OK);
    Bind(wxEVT_CHAR_HOOK, &ColumnsDialog::OnCharHook, this);

    choice_->SetSelection(0);
    ShowSet(0);
    list_->SetFocus();
}

bool ColumnsDialog::SaveChanges() {
    CaptureChecks();
    bool changed = false;
    for (size_t i = 0; i < sets_.size(); ++i) {
        if (layouts_[i] == saved_[i]) continue;
        SaveColumnLayout(*sets_[i], layouts_[i]);
        changed = true;
    }
    return changed;
}

void ColumnsDialog::ShowSet(size_t index) {
    current_ = index;
    const ColumnSet&    set    = *sets_[index];
    const ColumnLayout& layout = layouts_[index];
    list_->DeleteAllItems();
    for (size_t i = 0; i < layout.size(); ++i) {
        const long row = static_cast<long>(i);
        list_->InsertItem(row, set.columns[layout[i].id].name);
        list_->CheckItem(row, layout[i].shown);
    }
    if (!layout.empty()) SelectOnlyRow(list_, 0);
}

void ColumnsDialog::CaptureChecks() {
    ColumnLayout& layout = layouts_[current_];
    for (size_t i = 0; i < layout.size(); ++i) {
        layout[i].shown = list_->IsItemChecked(static_cast<long>(i));
    }
}

void ColumnsDialog::RefreshRows(size_t from, size_t to) {
    const ColumnSet&    set    = *sets_[current_];
    const ColumnLayout& layout = layouts_[current_];
    for (size_t i = from; i <= to; ++i) {
        const long row = static_cast<long>(i);
        list_->SetItemText(row, set.columns[layout[i].id].name);
        list_->CheckItem(row, layout[i].shown);
    }
}

void ColumnsDialog::MoveSelected(Shift shift) {
    ColumnLayout& layout   = layouts_[current_];
    const long    selected = list_->GetFirstSelected();
    if (selected < 0 || static_cast<size_t>(selected) >= layout.size()) return;

    const size_t from = static_cast<size_t>(selected);
    const size_t last = layout.size() - 1;
    size_t       to   = from;
    switch (shift) {
        case Shift::Up:     to = from == 0 ? 0 : from - 1; break;
        case Shift::Down:   to = std::min(from + 1, last); break;
        case Shift::Top:    to = 0; break;
        case Shift::Bottom: to = last; break;
    }
    if (to == from) return;

    CaptureChecks();
    const auto at = [&layout](size_t i) {
        return layout.begin() + static_cast<std::ptrdiff_t>(i);
    };
    if (to < from) std::rotate(at(to), at(from), at(from + 1));
    else           std::rotate(at(from), at(from + 1), at(to + 1));
    RefreshRows(std::min(from, to), std::max(from, to));
    SelectOnlyRow(list_, static_cast<long>(to));
    list_->EnsureVisible(static_cast<long>(to));
}

bool ColumnsDialog::ListHasFocus() const {
    const wxWindow* focus = FindFocus();
    return focus && (focus == list_ || focus->GetParent() == list_);
}

void ColumnsDialog::OnOk(wxCommandEvent& event) {
    CaptureChecks();
    for (size_t i = 0; i < layouts_.size(); ++i) {
        if (AnyColumnShown(layouts_[i])) continue;
        ShowError(this, L"Configure columns",
                  std::wstring(L"At least one column must be shown in ") +
                      sets_[i]->title + L".");
        choice_->SetSelection(static_cast<int>(i));
        ShowSet(i);
        list_->SetFocus();
        return;
    }
    event.Skip();
}

void ColumnsDialog::OnCharHook(wxKeyEvent& event) {
    if (event.GetModifiers() == wxMOD_CONTROL && ListHasFocus()) {
        switch (event.GetKeyCode()) {
            case WXK_UP:   MoveSelected(Shift::Up);     return;
            case WXK_DOWN: MoveSelected(Shift::Down);   return;
            case WXK_HOME: MoveSelected(Shift::Top);    return;
            case WXK_END:  MoveSelected(Shift::Bottom); return;
        }
    }
    event.Skip();
}

}

bool ShowColumnsDialog(wxWindow* owner) {
    ColumnsDialog dialog(owner);
    return dialog.ShowModal() == wxID_OK && dialog.SaveChanges();
}

}

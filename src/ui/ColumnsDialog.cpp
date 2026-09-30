#include "ui/ColumnsDialog.hpp"

#include "ui/App.hpp"
#include "ui/Columns.hpp"
#include "ui/ListView.hpp"
#include "ui/Widgets.hpp"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
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

struct MoveCommand {
    Shift          shift;
    const wchar_t* label;
    int            key;
};

constexpr MoveCommand kMoves[] = {
    {Shift::Up,     L"Move &up",        WXK_UP},
    {Shift::Down,   L"Move &down",      WXK_DOWN},
    {Shift::Top,    L"Move to &top",    WXK_HOME},
    {Shift::Bottom, L"Move to &bottom", WXK_END},
};

class ColumnsDialog : public wxDialog {
public:
    explicit ColumnsDialog(wxWindow* owner);

    bool SaveChanges();

private:
    void ShowSet(size_t index);
    void CaptureChecks();
    void RefreshRows(size_t from, size_t to);
    void MoveSelected(Shift shift);
    void OnOk(wxCommandEvent& event);
    void OnListShortcut(wxKeyEvent& event);

    std::span<const ColumnSet* const> sets_;
    std::vector<ColumnLayout>         saved_;
    std::vector<ColumnLayout>         layouts_;
    size_t                            current_ = 0;
    wxChoice*                         choice_  = nullptr;
    CheckList*                        list_    = nullptr;
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
    list_ = new CheckList(this, FromDIP(wxSize(260, 220)));

    auto* buttons = new wxBoxSizer(wxVERTICAL);
    for (const MoveCommand& move : kMoves) {
        auto* button = new wxButton(this, wxID_ANY, move.label);
        button->Bind(wxEVT_BUTTON, [this, shift = move.shift](wxCommandEvent&) {
            MoveSelected(shift);
        });
        buttons->Add(button, 0, wxEXPAND | wxBOTTOM, gap / 2);
    }

    auto* listRow = new wxBoxSizer(wxHORIZONTAL);
    listRow->Add(list_, 1, wxEXPAND);
    listRow->Add(buttons, 0, wxLEFT, gap);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(LabeledRow(choiceLabel, choice_, gap), 0, wxEXPAND | wxALL, gap);
    sizer->Add(listLabel, 0, wxLEFT | wxRIGHT, gap);
    sizer->Add(listRow, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    FinishDialog(*this, sizer, gap);

    choice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const int index = choice_->GetSelection();
        if (index < 0) return;
        CaptureChecks();
        ShowSet(static_cast<size_t>(index));
    });
    Bind(wxEVT_BUTTON, &ColumnsDialog::OnOk, this, wxID_OK);
    Bind(wxEVT_CHAR_HOOK, &ColumnsDialog::OnListShortcut, this);

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
    list_->ClearRows();
    for (const ColumnState& c : layout) {
        list_->AppendRow(set.columns[c.id].name, c.shown);
    }
    if (!layout.empty()) list_->SelectOnly(0);
}

void ColumnsDialog::CaptureChecks() {
    ColumnLayout& layout = layouts_[current_];
    for (size_t i = 0; i < layout.size(); ++i) {
        layout[i].shown = list_->IsRowChecked(static_cast<long>(i));
    }
}

void ColumnsDialog::RefreshRows(size_t from, size_t to) {
    const ColumnSet&    set    = *sets_[current_];
    const ColumnLayout& layout = layouts_[current_];
    for (size_t i = from; i <= to; ++i) {
        list_->SetRow(static_cast<long>(i), set.columns[layout[i].id].name,
                      layout[i].shown);
    }
}

void ColumnsDialog::MoveSelected(Shift shift) {
    ColumnLayout& layout   = layouts_[current_];
    const long    selected = list_->SelectedRow();
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
    list_->SelectOnly(static_cast<long>(to));
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

void ColumnsDialog::OnListShortcut(wxKeyEvent& event) {
    if (HasFocusWithin(list_)) {
        for (const MoveCommand& move : kMoves) {
            if (IsKey(event, move.key, wxMOD_CONTROL)) {
                MoveSelected(move.shift);
                return;
            }
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

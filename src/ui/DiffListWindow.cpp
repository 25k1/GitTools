#include "git/Diff.hpp"

#include "ui/DiffListWindow.hpp"

#include "ui/App.hpp"
#include "ui/ChangeList.hpp"
#include "ui/DiffWindow.hpp"
#include "ui/MenuFrame.hpp"

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace git_tools {

namespace {

std::wstring ListTitle(const std::wstring& origin) {
    return origin.empty() ? std::wstring(L"gittools - diff")
                          : L"gittools - diff - " + origin;
}

class DiffListFrame : public MenuFrame {
public:
    DiffListFrame(DiffListParams params, std::vector<FileDiff> files);

protected:
    std::vector<MenuSection> Menus() override;
    void OnOptionsChanged() override { list_->ApplyColumnLayout(); }

private:
    std::optional<ChangesDiff> DiffOf(const std::vector<size_t>& rows) const;

    DiffListParams        params_;
    std::vector<FileDiff> files_;
    ChangeList*           list_ = nullptr;
};

DiffListFrame::DiffListFrame(DiffListParams params, std::vector<FileDiff> files)
    : MenuFrame(nullptr, ListTitle(params.origin), wxSize(900, 560)),
      params_(std::move(params)),
      files_(std::move(files)) {
    auto*     panel = new wxPanel(this);
    const int gap   = FromDIP(4);

    auto* label = new wxStaticText(panel, wxID_ANY, L"C&hanges");
    list_ = new ChangeList(panel, params_.workTree,
                           [this](const std::vector<size_t>& rows) {
        return DiffOf(rows);
    });
    std::vector<FileChange> changes;
    for (const FileDiff& file : files_) changes.push_back(file.change);
    list_->SetChanges(std::move(changes));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    sizer->Add(list_, 1, wxEXPAND | wxALL, gap);
    panel->SetSizer(sizer);

    BuildMenus();
    list_->SelectOnly(0);
    list_->SetFocus();
}

std::vector<MenuSection> DiffListFrame::Menus() {
    return {
        {L"&File", WithFileCommands(list_->FileEntries())},
        {L"&Edit", list_->EditEntries()},
    };
}

std::optional<ChangesDiff> DiffListFrame::DiffOf(const std::vector<size_t>& rows) const {
    const std::wstring_view diff = params_.diffText;
    std::wstring            text;
    for (size_t row : rows) {
        text += diff.substr(files_[row].offset, files_[row].length);
    }
    return ChangesDiff{std::move(text), params_.origin};
}

}

void ShowDiffList(DiffListParams params) {
    std::vector<FileDiff> files = SplitFileDiffs(params.diffText);
    if (files.empty()) {
        ShowDiffWindow(nullptr, {ListTitle(params.origin), std::move(params.diffText),
                                 std::move(params.workTree)});
        return;
    }
    ShowOnActiveDisplay(new DiffListFrame(std::move(params), std::move(files)));
}

}

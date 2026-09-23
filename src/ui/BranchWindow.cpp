#include "ui/BranchWindow.hpp"

#include "git/Git.hpp"

#include "ui/App.hpp"
#include "ui/ToolFrame.hpp"
#include "ui/Widgets.hpp"

#include <wx/panel.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace git_tools {

namespace {

class BranchFrame : public ToolFrame {
public:
    explicit BranchFrame(BranchWindowParams params);

private:
    void ShowBranches();
    void Reload();
    void OnCheckout();

    BranchWindowParams params_;
    wxListView*        list_ = nullptr;
};

BranchFrame::BranchFrame(BranchWindowParams params)
    : ToolFrame(params.title, wxSize(960, 620)), params_(std::move(params)) {
    AddLabel(L"&Branches");
    list_ = new wxListView(Panel(), wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    AddPane(list_, 70);
    FinishLayout(list_, 30);

    AddColumns(list_, {
        {L"Name",     360},
        {L"State",     50},
        {L"Upstream", 200},
        {L"Subject",  400},
    });
    ShowBranches();

    list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { OnCheckout(); });
    list_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_F5 && event.GetModifiers() == wxMOD_NONE) {
            Reload();
            return;
        }
        event.Skip();
    });
    list_->SetFocus();
}

void BranchFrame::ShowBranches() {
    const std::vector<Branch>& branches = params_.branches;
    list_->DeleteAllItems();
    long row = 0;
    for (const Branch& b : branches) {
        const long item = list_->InsertItem(row++, b.name);
        list_->SetItem(item, 1, b.isCurrent ? L"*" : L"");
        list_->SetItem(item, 2, b.upstream);
        list_->SetItem(item, 3, b.subject);
    }
    if (branches.empty()) return;

    const auto current = std::ranges::find_if(branches, &Branch::isCurrent);
    const long focus   = current == branches.end()
                             ? 0
                             : static_cast<long>(current - branches.begin());
    list_->Select(focus);
    list_->Focus(focus);
}

void BranchFrame::Reload() {
    BranchListResult lr = LoadBranchList(params_.cwd);
    if (!lr.errorMessage.empty()) return;
    params_.branches = std::move(lr.branches);
    ShowBranches();
}

void BranchFrame::OnCheckout() {
    const long idx = SelectedIndexIn(list_, params_.branches.size());
    if (idx < 0) return;
    const Branch& b = params_.branches[static_cast<size_t>(idx)];
    if (b.isCurrent) return;

    std::wstring target = b.name;
    if (const size_t slash = target.find(L'/');
        b.isRemote && slash != std::wstring::npos) {
        target.erase(0, slash + 1);
    }
    ProcessResult r = CheckoutBranch(target, params_.cwd);
    if (!r.started) {
        ShowError(this, L"Checkout failed",
                  L"Failed to launch git:\n\n" + r.errorMessage);
        return;
    }
    if (r.exitCode != 0) {
        ShowError(this, L"Checkout failed",
                  L"git checkout " + b.name + L" failed:\n\n" +
                      Utf8ToWide(r.stderrText.empty() ? r.stdoutText
                                                      : r.stderrText));
        return;
    }
    Reload();
}

}

int ShowBranchWindow(const BranchWindowParams& params) {
    ShowOnActiveDisplay(new BranchFrame(params));
    return 0;
}

}

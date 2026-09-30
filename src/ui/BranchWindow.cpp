#include "ui/BranchWindow.hpp"

#include "git/Git.hpp"

#include "ui/App.hpp"
#include "ui/Columns.hpp"
#include "ui/ListView.hpp"
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

protected:
    void OnOptionsChanged() override;

private:
    std::wstring BranchCell(long row, long column) const;
    void ShowBranches();
    void Reload();
    void OnCheckout();

    BranchWindowParams params_;
    VirtualList*       list_ = nullptr;
};

BranchFrame::BranchFrame(BranchWindowParams params)
    : ToolFrame(params.title, wxSize(960, 620)), params_(std::move(params)) {
    AddLabel(L"&Branches");
    list_ = new VirtualList(Panel(), false, kBranchColumns,
                            [this](long row, long column) {
        return BranchCell(row, column);
    });
    AddPane(list_, 70);
    FinishLayout(list_, 30);
    ShowBranches();

    list_->WhenActivated([this] { OnCheckout(); });
    BindKey(list_, WXK_F5, wxMOD_NONE, [this] { Reload(); });
    list_->SetFocus();
}

void BranchFrame::OnOptionsChanged() {
    list_->ApplyColumnLayout();
}

std::wstring BranchFrame::BranchCell(long row, long column) const {
    const Branch* b = RowAt(params_.branches, row);
    if (!b) return {};
    switch (column) {
        case kBranchName:     return b->name;
        case kBranchState:    return b->isCurrent ? L"*" : L"";
        case kBranchUpstream: return b->upstream;
        case kBranchSubject:  return b->subject;
        default: return {};
    }
}

void BranchFrame::ShowBranches() {
    const std::vector<Branch>& branches = params_.branches;
    list_->ResetRows(branches.size());
    if (branches.empty()) return;

    const auto current = std::ranges::find_if(branches, &Branch::isCurrent);
    const long focus   = current == branches.end()
                             ? 0
                             : static_cast<long>(current - branches.begin());
    list_->SelectOnly(focus);
}

void BranchFrame::Reload() {
    BranchListResult lr = LoadBranchList(params_.cwd);
    if (!lr.errorMessage.empty()) return;
    params_.branches = std::move(lr.branches);
    ShowBranches();
}

void BranchFrame::OnCheckout() {
    const Branch* b = RowAt(params_.branches, list_->SelectedRow());
    if (!b || b->isCurrent) return;

    std::wstring target = b->name;
    if (const size_t slash = target.find(L'/');
        b->isRemote && slash != std::wstring::npos) {
        target.erase(0, slash + 1);
    }
    const ProcessResult r = CheckoutBranch(target, params_.cwd);
    if (!r.ok()) {
        ShowError(this, L"Checkout failed", GitFailure(L"git checkout " + target, r));
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

#pragma once

#include "git/Types.hpp"
#include "ui/ListView.hpp"
#include "ui/Widgets.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace git_tools {

struct ChangesDiff {
    std::wstring text;
    std::wstring origin;
};

class ChangeList : public VirtualList {
public:
    using DiffSource =
        std::function<std::optional<ChangesDiff>(const std::vector<size_t>& rows)>;

    ChangeList(wxWindow* parent, std::wstring workTree, DiffSource source);

    const std::vector<FileChange>& Changes() const { return changes_; }
    void SetChanges(std::vector<FileChange> changes);

    void WhenReload(std::function<void()> fn);

    MenuEntries FileEntries();
    MenuEntries EditEntries();

private:
    std::wstring        Cell(long row, long column) const;
    std::vector<size_t> SelectedChangeRows() const;
    std::wstring        SelectedFilePath() const;

    void OpenDiff();
    void CopyPaths(bool full);
    void OnKey(wxKeyEvent& event);
    void ShowContextMenu(const wxPoint& at);

    std::wstring            workTree_;
    DiffSource              source_;
    std::vector<FileChange> changes_;
};

}

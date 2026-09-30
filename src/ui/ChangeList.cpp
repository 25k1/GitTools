#include "ui/Shell.hpp"
#include "util/Text.hpp"

#include "ui/ChangeList.hpp"

#include "ui/App.hpp"
#include "ui/Columns.hpp"
#include "ui/DiffWindow.hpp"

#include <wx/window.h>

#include <utility>

namespace git_tools {

namespace {

std::wstring FormatCount(int value, bool suppressed) {
    if (suppressed)  return L"";
    if (value == -2) return L"bin";
    return value < 0 ? std::wstring() : std::to_wstring(value);
}

std::wstring ChangeName(const FileChange& fc) {
    return HasOldPath(fc.kind) && !fc.oldPath.empty()
               ? fc.oldPath + L" -> " + fc.path
               : fc.path;
}

std::wstring ChangeCell(const FileChange& fc, long column) {
    switch (column) {
        case kChangeName:  return ChangeName(fc);
        case kChangeState: return std::wstring(1, fc.kindChar);
        case kChangeInsertions:
            return FormatCount(fc.insertions, fc.kind == FileChangeKind::Deleted);
        case kChangeDeletions:
            return FormatCount(fc.deletions, fc.kind == FileChangeKind::Added);
        default: return {};
    }
}

}

ChangeList::ChangeList(wxWindow* parent, std::wstring workTree, DiffSource source)
    : VirtualList(parent, true, kChangeColumns,
                  [this](long row, long column) { return Cell(row, column); }),
      workTree_(std::move(workTree)),
      source_(std::move(source)) {
    Bind(wxEVT_KEY_DOWN, &ChangeList::OnKey, this);
    WhenActivated([this] { OpenDiff(); });
    WhenContextMenu([this](const wxPoint& at) { ShowContextMenu(at); });
}

void ChangeList::SetChanges(std::vector<FileChange> changes) {
    changes_ = std::move(changes);
    ResetRows(changes_.size());
}

std::wstring ChangeList::Cell(long row, long column) const {
    const FileChange* fc = RowAt(changes_, row);
    return fc ? ChangeCell(*fc, column) : std::wstring();
}

MenuEntries ChangeList::FileEntries() {
    const bool         selected = !SelectedChangeRows().empty();
    const std::wstring path     = SelectedFilePath();
    return {
        {L"&View diff\tEnter", [this] { OpenDiff(); }, selected},
        {L"&Open file location", [this] {
             const std::wstring file = SelectedFilePath();
             if (!file.empty()) RevealFile(wxGetTopLevelParent(this), file);
         }, PathExists(ParentDirectory(path))},
        {L"&Edit file", [this] { EditFile(wxGetTopLevelParent(this), SelectedFilePath()); },
         !FindEditor().empty() && PathExists(path)},
    };
}

MenuEntries ChangeList::EditEntries() {
    const bool selected = !SelectedChangeRows().empty();
    return {
        {L"Copy &path\tCtrl+C", [this] { CopyPaths(false); }, selected},
        {L"Copy file &location\tCtrl+Shift+C", [this] { CopyPaths(true); }, selected},
        {L"Select &all\tCtrl+A", [this] { SelectAllRows(); }, !changes_.empty()},
    };
}

std::vector<size_t> ChangeList::SelectedChangeRows() const {
    std::vector<size_t> rows;
    for (long row : SelectedRows()) {
        if (RowWithin(row, changes_.size()) >= 0) {
            rows.push_back(static_cast<size_t>(row));
        }
    }
    return rows;
}

void ChangeList::OpenDiff() {
    const std::vector<size_t> rows = SelectedChangeRows();
    if (rows.empty() || !source_) return;
    const std::optional<ChangesDiff> diff = source_(rows);
    if (!diff) return;

    DiffWindowParams p;
    p.title = L"Diff: " + (rows.size() == 1 ? changes_[rows.front()].path
                                            : std::to_wstring(rows.size()) +
                                                  L" files");
    if (!diff->origin.empty()) p.title += L" - " + diff->origin;
    p.diffText = diff->text;
    p.workTree = workTree_;
    ShowDiffWindow(wxGetTopLevelParent(this), p);
}

void ChangeList::CopyPaths(bool full) {
    std::vector<std::wstring> paths;
    for (size_t row : SelectedChangeRows()) {
        const std::wstring& path = changes_[row].path;
        const std::wstring  location = full ? RepoFilePath(workTree_, path)
                                            : std::wstring();
        paths.push_back(location.empty() ? path : location);
    }
    if (!paths.empty()) SetClipboardText(Join(paths, L"\r\n"));
}

std::wstring ChangeList::SelectedFilePath() const {
    const std::vector<size_t> rows = SelectedChangeRows();
    return rows.empty() ? std::wstring()
                        : RepoFilePath(workTree_, changes_[rows.front()].path);
}

void ChangeList::WhenReload(std::function<void()> fn) {
    BindKey(this, WXK_F5, wxMOD_NONE, std::move(fn));
}

void ChangeList::OnKey(wxKeyEvent& event) {
    if (!RunShortcut(event, EditEntries())) event.Skip();
}

void ChangeList::ShowContextMenu(const wxPoint& at) {
    if (SelectedChangeRows().empty()) return;
    ShowPopupMenu(this, at, AppendGroup(FileEntries(), EditEntries()));
}

}

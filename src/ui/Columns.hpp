#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace git_tools {

struct ColumnDef {
    const wchar_t* key;
    const wchar_t* name;
    int            width;
    bool           right = false;
};

struct ColumnSet {
    const wchar_t*             title;
    const wchar_t*             configKey;
    std::span<const ColumnDef> columns;
};

struct ColumnState {
    size_t id    = 0;
    bool   shown = true;

    bool operator==(const ColumnState&) const = default;
};

using ColumnLayout = std::vector<ColumnState>;

enum CommitColumn {
    kCommitSubject,
    kCommitAuthor,
    kCommitDate,
    kCommitInsertions,
    kCommitDeletions,
};

enum ChangeColumn {
    kChangeName,
    kChangeState,
    kChangeInsertions,
    kChangeDeletions,
};

enum BranchColumn {
    kBranchName,
    kBranchState,
    kBranchUpstream,
    kBranchSubject,
};

extern const ColumnSet kCommitColumns;
extern const ColumnSet kChangeColumns;
extern const ColumnSet kBranchColumns;

std::span<const ColumnSet* const> ColumnSets();

ColumnLayout LoadColumnLayout(const ColumnSet& set);

std::vector<size_t> VisibleColumns(const ColumnSet& set);

void SaveColumnLayout(const ColumnSet& set, const ColumnLayout& layout);

bool AnyColumnShown(const ColumnLayout& layout);

}

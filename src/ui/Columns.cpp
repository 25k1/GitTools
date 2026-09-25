#include "ui/Columns.hpp"

#include "git/Config.hpp"
#include "util/Text.hpp"

#include <algorithm>
#include <string>

namespace git_tools {

namespace {

constexpr wchar_t kHiddenPrefix = L'-';

constexpr ColumnDef kCommitDefs[] = {
    {L"subject",    L"Subject",    520},
    {L"author",     L"Author",     160},
    {L"date",       L"Date",       140},
    {L"insertions", L"Insertions",  80, true},
    {L"deletions",  L"Deletions",   80, true},
};

constexpr ColumnDef kChangeDefs[] = {
    {L"name",       L"Name",       540},
    {L"state",      L"State",       60},
    {L"insertions", L"Insertions",  80, true},
    {L"deletions",  L"Deletions",   80, true},
};

constexpr ColumnDef kBranchDefs[] = {
    {L"name",     L"Name",     360},
    {L"state",    L"State",     50},
    {L"upstream", L"Upstream", 200},
    {L"subject",  L"Subject",  400},
};

}

const ColumnSet kCommitColumns{L"Commits",  L"commitcolumns", kCommitDefs};
const ColumnSet kChangeColumns{L"Changes",  L"changecolumns", kChangeDefs};
const ColumnSet kBranchColumns{L"Branches", L"branchcolumns", kBranchDefs};

std::span<const ColumnSet* const> ColumnSets() {
    static const ColumnSet* const sets[] = {
        &kCommitColumns,
        &kChangeColumns,
        &kBranchColumns,
    };
    return sets;
}

ColumnLayout LoadColumnLayout(const ColumnSet& set) {
    const size_t      count = set.columns.size();
    std::vector<bool> seen(count, false);
    ColumnLayout      layout;
    for (const std::wstring& raw : Split(ConfigGet(set.configKey), L',')) {
        std::wstring token  = Trim(raw);
        const bool   hidden = !token.empty() && token.front() == kHiddenPrefix;
        if (hidden) token.erase(0, 1);
        for (size_t id = 0; id < count; ++id) {
            if (seen[id] || token != set.columns[id].key) continue;
            seen[id] = true;
            layout.push_back({id, !hidden});
            break;
        }
    }
    for (size_t id = 0; id < count; ++id) {
        if (!seen[id]) layout.push_back({id, true});
    }
    if (!layout.empty() && !AnyColumnShown(layout)) layout.front().shown = true;
    return layout;
}

void SaveColumnLayout(const ColumnSet& set, const ColumnLayout& layout) {
    std::vector<std::wstring> tokens;
    for (const ColumnState& c : layout) {
        std::wstring token = c.shown ? std::wstring() : std::wstring(1, kHiddenPrefix);
        tokens.push_back(token + set.columns[c.id].key);
    }
    ConfigSet(set.configKey, Join(tokens, L","));
}

bool AnyColumnShown(const ColumnLayout& layout) {
    return std::ranges::any_of(layout, &ColumnState::shown);
}

}

#pragma once

#include "git/Types.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace git_tools {

struct FileDiff {
    FileChange change;
    size_t     offset = 0;
    size_t     length = 0;
    size_t     line   = 0;
};

struct DiffLocation {
    std::wstring path;
    int          line = 0;
    std::wstring content;
};

std::vector<FileDiff> SplitFileDiffs(std::wstring_view diff);

std::wstring SeparateFileDiffs(std::wstring_view text);

DiffLocation LocateInDiff(std::wstring_view text,
                          const std::vector<FileDiff>& files, size_t caretLine);

std::vector<size_t> MarkerWidths(const std::vector<std::wstring>& lines);

int LineInFile(const DiffLocation& loc, const std::wstring& path);

}

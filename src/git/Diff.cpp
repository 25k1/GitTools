#include "git/Diff.hpp"

#include "util/Encoding.hpp"
#include "util/System.hpp"
#include "util/Text.hpp"

#include <algorithm>
#include <climits>

namespace git_tools {

namespace {

constexpr std::wstring_view kGitHeader        = L"diff --git ";
constexpr std::wstring_view kCombinedHeaders[] = {L"diff --cc ", L"diff --combined "};

size_t CombinedHeaderSize(std::wstring_view line) {
    for (std::wstring_view header : kCombinedHeaders) {
        if (line.starts_with(header)) return header.size();
    }
    return 0;
}

bool IsFileDiffHeader(std::wstring_view line) {
    return line.starts_with(kGitHeader) || CombinedHeaderSize(line) > 0;
}

size_t HunkMarkerWidth(std::wstring_view line) {
    if (!line.starts_with(L"@@")) return 0;
    const size_t ats = line.find_first_not_of(L'@');
    return ats == line.npos ? 0 : ats - 1;
}

bool IsOctal(wchar_t c) {
    return c >= L'0' && c <= L'7';
}

std::string EscapedChar(wchar_t c) {
    switch (c) {
        case L'a': return "\a";
        case L'b': return "\b";
        case L'f': return "\f";
        case L'n': return "\n";
        case L'r': return "\r";
        case L't': return "\t";
        case L'v': return "\v";
        default:   return WideToUtf8(std::wstring_view(&c, 1));
    }
}

std::wstring UnquotePath(std::wstring_view p) {
    if (p.size() < 2 || p.front() != L'"' || p.back() != L'"') {
        return std::wstring(p);
    }
    p = p.substr(1, p.size() - 2);
    std::string bytes;
    for (size_t i = 0; i < p.size();) {
        if (p[i] != L'\\' || i + 1 == p.size()) {
            const size_t end = std::min(p.find(L'\\', i + 1), p.size());
            bytes += WideToUtf8(p.substr(i, end - i));
            i = end;
            continue;
        }
        ++i;
        if (!IsOctal(p[i])) {
            bytes += EscapedChar(p[i++]);
            continue;
        }
        int value = 0;
        for (int n = 0; n < 3 && i < p.size() && IsOctal(p[i]); ++n, ++i) {
            value = value * 8 + (p[i] - L'0');
        }
        bytes += static_cast<char>(value);
    }
    return Utf8ToWide(bytes);
}

size_t QuotedEnd(std::wstring_view s) {
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i] == L'\\') {
            ++i;
        } else if (s[i] == L'"') {
            return i + 1;
        }
    }
    return s.npos;
}

std::wstring WithoutPrefix(std::wstring_view token) {
    std::wstring path = UnquotePath(token);
    if (path.size() > 2 && path[1] == L'/') path.erase(0, 2);
    return path;
}

void SplitPaths(std::wstring_view rest, std::wstring& oldPath,
                std::wstring& newPath) {
    size_t split = rest.npos;
    if (rest.starts_with(L'"')) {
        split = QuotedEnd(rest);
    } else if (const size_t quote = rest.find(L" \""); quote != rest.npos) {
        split = quote;
    } else if (rest.size() % 2 == 1 && rest[rest.size() / 2] == L' ' &&
               WithoutPrefix(rest.substr(0, rest.size() / 2)) ==
                   WithoutPrefix(rest.substr(rest.size() / 2 + 1))) {
        split = rest.size() / 2;
    } else {
        split = rest.rfind(L" b/");
    }
    if (split == rest.npos || split >= rest.size()) {
        oldPath = newPath = WithoutPrefix(rest);
        return;
    }
    oldPath = WithoutPrefix(rest.substr(0, split));
    newPath = WithoutPrefix(rest.substr(split + 1));
}

void SetKind(FileChange& fc, wchar_t kindChar) {
    fc.kind     = KindFromChar(kindChar);
    fc.kindChar = kindChar;
}

FileChange StartFile(std::wstring_view header) {
    FileChange fc;
    SetKind(fc, L'M');
    fc.insertions = 0;
    fc.deletions  = 0;
    if (header.starts_with(kGitHeader)) {
        std::wstring oldPath;
        SplitPaths(header.substr(kGitHeader.size()), oldPath, fc.path);
        if (oldPath != fc.path) fc.oldPath = std::move(oldPath);
    } else {
        fc.path = UnquotePath(header.substr(CombinedHeaderSize(header)));
    }
    return fc;
}

struct PathHeader {
    std::wstring_view prefix;
    wchar_t           kind;
    bool              old;
};

constexpr PathHeader kPathHeaders[] = {
    {L"rename from ", L'R', true},
    {L"rename to ",   L'R', false},
    {L"copy from ",   L'C', true},
    {L"copy to ",     L'C', false},
};

void NoteHeaderLine(FileChange& fc, std::wstring_view line) {
    for (const PathHeader& header : kPathHeaders) {
        if (!line.starts_with(header.prefix)) continue;
        SetKind(fc, header.kind);
        (header.old ? fc.oldPath : fc.path) = UnquotePath(line.substr(header.prefix.size()));
        return;
    }
    if (line.starts_with(L"new file mode") || line == L"--- /dev/null") {
        SetKind(fc, L'A');
    } else if (line.starts_with(L"deleted file mode") || line == L"+++ /dev/null") {
        SetKind(fc, L'D');
    } else if (line.starts_with(L"Binary files ") ||
               line.starts_with(L"GIT binary patch")) {
        fc.insertions = -2;
        fc.deletions  = -2;
    }
}

void NoteLine(FileChange& fc, std::wstring_view line, size_t& width) {
    if (const size_t hunk = HunkMarkerWidth(line); hunk > 0) {
        width = hunk;
        return;
    }
    if (width == 0) {
        NoteHeaderLine(fc, line);
        return;
    }
    const std::wstring_view marks = line.substr(0, width);
    if (marks.find(L'+') != marks.npos) {
        ++fc.insertions;
    } else if (marks.find(L'-') != marks.npos) {
        ++fc.deletions;
    }
}

double Similarity(const std::wstring& a, const std::wstring& b) {
    const size_t n = std::min<size_t>(a.size(), 256);
    const size_t m = std::min<size_t>(b.size(), 256);
    if (n == 0 || m == 0) return (n == m) ? 1.0 : 0.0;

    std::vector<int> prev(m + 1);
    std::vector<int> cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        prev.swap(cur);
    }
    return 1.0 - static_cast<double>(prev[m]) /
                     static_cast<double>(std::max(n, m));
}

int MatchLine(const std::vector<std::wstring>& lines,
                    const std::wstring& needle, int estimate) {
    if (lines.empty() || needle.empty()) return 0;

    const int count  = static_cast<int>(lines.size());
    const int center = std::min(std::max(estimate, 1) - 1, count - 1);

    for (int step = 0; step < count; ++step) {
        const int before = center - step;
        const int after  = center + step;
        if (before >= 0 && lines[before] == needle) return before + 1;
        if (after != before && after < count && lines[after] == needle) {
            return after + 1;
        }
        if (before < 0 && after >= count) break;
    }

    constexpr int    kWindow   = 300;
    constexpr double kMinScore = 0.6;
    double best     = 0.0;
    int    bestLine = 0;
    for (int step = 0; step <= kWindow; ++step) {
        for (int side : {-1, 1}) {
            const int i = center + side * step;
            if ((step == 0 && side > 0) || i < 0 || i >= count) continue;
            if (const double score = Similarity(lines[i], needle); score > best) {
                best     = score;
                bestLine = i + 1;
            }
        }
    }
    return (best >= kMinScore) ? bestLine : 0;
}

int LeadingNumber(std::wstring_view s) {
    long long value = 0;
    ParseDigits(s.substr(0, s.find_first_not_of(L"0123456789")), value);
    return static_cast<int>(std::min<long long>(value, INT_MAX));
}

bool IsDiffBodyLine(std::wstring_view line, bool includeRemoved) {
    return !line.empty() &&
           (line[0] == L' ' || line[0] == L'+' ||
            (includeRemoved && line[0] == L'-'));
}

}

std::vector<FileDiff> SplitFileDiffs(std::wstring_view diff) {
    std::vector<FileDiff> files;
    size_t                width = 0;
    size_t                index = 0;

    const auto finish = [&](size_t end) {
        if (!files.empty()) files.back().length = end - files.back().offset;
    };

    for (size_t pos = 0; pos < diff.size(); ++index) {
        const size_t eol  = diff.find(L'\n', pos);
        const size_t next = eol == diff.npos ? diff.size() : eol + 1;
        std::wstring_view line =
            diff.substr(pos, (eol == diff.npos ? diff.size() : eol) - pos);
        if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);

        if (IsFileDiffHeader(line)) {
            finish(pos);
            width = 0;
            files.push_back({StartFile(line), pos, 0, index});
        } else if (!files.empty()) {
            NoteLine(files.back().change, line, width);
        }
        pos = next;
    }
    finish(diff.size());
    return files;
}

std::wstring SeparateFileDiffs(std::wstring_view text) {
    std::wstring out;
    size_t       done = 0;
    for (const FileDiff& file : SplitFileDiffs(text)) {
        out += text.substr(done, file.offset - done);
        if (!out.empty()) out += out.back() == L'\n' ? L"\n\n" : L"\n\n\n";
        done = file.offset;
    }
    return out += text.substr(done);
}

DiffLocation LocateInDiff(std::wstring_view text,
                          const std::vector<FileDiff>& files, size_t caretLine) {
    DiffLocation loc;
    auto file = std::ranges::upper_bound(files, caretLine, {}, &FileDiff::line);
    if (file == files.begin()) return loc;
    --file;
    loc.path = file->change.path;

    size_t index   = file->line;
    int    newLine = 0;
    bool   inHunk  = false;
    ForEachLine(text.substr(file->offset, file->length), [&](std::wstring_view line) {
        bool content = false;
        if (HunkMarkerWidth(line) > 0) {
            if (const size_t plus = line.find(L'+'); plus != line.npos) {
                newLine = LeadingNumber(line.substr(plus + 1));
                inHunk  = true;
            }
        } else {
            content = inHunk;
        }

        if (index++ == caretLine) {
            if (inHunk) {
                loc.line = newLine;
                if (IsDiffBodyLine(line, true)) loc.content = line.substr(1);
            }
            return false;
        }
        if (content && (line.empty() || IsDiffBodyLine(line, false))) ++newLine;
        return true;
    });
    return loc;
}

std::vector<size_t> MarkerWidths(const std::vector<std::wstring>& lines) {
    std::vector<size_t> widths(lines.size(), 0);
    size_t width = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring_view line = lines[i];
        if (IsFileDiffHeader(line)) {
            width = 0;
        } else if (const size_t hunk = HunkMarkerWidth(line); hunk > 0) {
            width = hunk;
        } else if (width > 0 && line.size() >= width &&
                   line.substr(0, width).find_first_not_of(L" +-") == line.npos) {
            widths[i] = width;
        }
    }
    return widths;
}

int LineInFile(const DiffLocation& loc, const std::wstring& path) {
    if (loc.content.empty()) return loc.line;
    const std::wstring body = Utf8ToWide(ReadFileBytes(path));
    return body.empty() ? 0 : MatchLine(SplitLines(body), loc.content, loc.line);
}

}

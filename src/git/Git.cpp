#include "git/Git.hpp"

#include "git/Transcript.hpp"
#include "ui/Encoding.hpp"

#include <unordered_map>

namespace git_tools {

ProcessResult RunGit(const std::vector<std::wstring>& args,
                     const std::wstring& cwd,
                     StdioMode stdio,
                     ProcessCanceller* cancel) {
    NoteGitStart(args);
    ProcessResult r = RunProcess(L"git.exe", args, cwd, stdio, cancel);
    if (cancel && cancel->Cancelled()) RecordGitCancelled(args);
    else                               RecordGitRun(args, r);
    return r;
}

RepoContext OpenRepo() {
    RepoContext repo;
    repo.cwd = CurrentDirectory();

    ProcessResult top = RunGit({L"rev-parse", L"--show-toplevel"}, repo.cwd);
    if (!top.started) {
        repo.errorMessage = L"Failed to launch git:\n\n" + top.errorMessage;
    } else if (top.exitCode != 0) {
        repo.errorMessage = L"Not inside a git repository:\n\n" +
                            Utf8ToWide(RStrip(top.stderrText));
    } else {
        repo.root = TrimmedOutput(top);
    }
    return repo;
}

namespace {

constexpr wchar_t kDiffMerges[] = L"--diff-merges=first-parent";

std::wstring GitFailure(const wchar_t* what, const ProcessResult& r) {
    if (!r.started) return r.errorMessage;
    if (r.exitCode == 0) return {};
    return std::wstring(what) + L" failed (exit " +
           std::to_wstring(r.exitCode) + L"):\n" + Utf8ToWide(r.stderrText);
}

std::vector<std::wstring> SplitOn(const std::wstring& s, wchar_t delim) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == delim) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

FileChangeKind KindFromChar(wchar_t c) {
    switch (c) {
        case L'A': return FileChangeKind::Added;
        case L'D': return FileChangeKind::Deleted;
        case L'M': return FileChangeKind::Modified;
        case L'R': return FileChangeKind::Renamed;
        case L'C': return FileChangeKind::Copied;
        case L'T': return FileChangeKind::TypeChanged;
        default:   return FileChangeKind::Other;
    }
}

template <typename F>
void ForEachLine(const std::wstring& text, F&& fn) {
    std::wstring line;
    for (wchar_t ch : text) {
        if (ch == L'\n') { if (!line.empty()) { fn(line); line.clear(); } }
        else if (ch != L'\r') line.push_back(ch);
    }
    if (!line.empty()) fn(line);
}

std::wstring NormalizeNumstatPath(const std::wstring& p) {
    size_t arrow = p.find(L" => ");
    if (arrow == std::wstring::npos) return p;
    size_t braceOpen = p.rfind(L'{', arrow);
    if (braceOpen != std::wstring::npos) {
        size_t braceClose = p.find(L'}', arrow);
        if (braceClose != std::wstring::npos) {
            std::wstring out = p.substr(0, braceOpen);
            out += p.substr(arrow + 4, braceClose - arrow - 4);
            out += p.substr(braceClose + 1);
            return out;
        }
    }
    return p.substr(arrow + 4);
}

struct NumStat {
    int insertions = -1;
    int deletions  = -1;
};

int ParseCount(const std::wstring& field) {
    if (field == L"-") return -2;
    if (field.empty()) return -1;
    long long value = 0;
    for (wchar_t c : field) {
        if (c < L'0' || c > L'9') return -1;
        value = value * 10 + (c - L'0');
        if (value > 1000000000) return 1000000000;
    }
    return static_cast<int>(value);
}

std::unordered_map<std::wstring, NumStat>
ParseNumstat(const std::wstring& output) {
    std::unordered_map<std::wstring, NumStat> result;
    ForEachLine(output, [&](const std::wstring& line) {
        std::vector<std::wstring> fields = SplitOn(line, L'\t');
        if (fields.size() >= 3) {
            NumStat ns;
            ns.insertions = ParseCount(fields[0]);
            ns.deletions  = ParseCount(fields[1]);
            result[NormalizeNumstatPath(fields[2])] = ns;
        }
    });
    return result;
}

std::vector<FileChange> ParseNameStatus(const std::wstring& output) {
    std::vector<FileChange> result;
    ForEachLine(output, [&](const std::wstring& line) {
        std::vector<std::wstring> fields = SplitOn(line, L'\t');
        if (fields.size() >= 2 && !fields[0].empty()) {
            FileChange fc;
            fc.kindChar = fields[0][0];
            fc.kind = KindFromChar(fc.kindChar);
            if ((fc.kind == FileChangeKind::Renamed ||
                 fc.kind == FileChangeKind::Copied) &&
                fields.size() >= 3) {
                fc.oldPath = std::move(fields[1]);
                fc.path    = std::move(fields[2]);
            } else {
                fc.path = std::move(fields[1]);
            }
            result.push_back(std::move(fc));
        }
    });
    return result;
}

std::wstring RStripW(std::wstring s) {
    while (!s.empty() &&
           (s.back() == L'\n' || s.back() == L'\r' ||
            s.back() == L' '  || s.back() == L'\t')) {
        s.pop_back();
    }
    return s;
}

std::wstring FirstLine(const std::wstring& s) {
    size_t nl = s.find(L'\n');
    return RStripW(nl == std::wstring::npos ? s : s.substr(0, nl));
}

bool StartsWith(const std::wstring& s, const wchar_t* prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool IsRefName(const std::wstring& name, const std::wstring& cwd) {
    ProcessResult r = RunGit(
        {L"rev-parse", L"--symbolic-full-name", L"--verify", L"-q", name}, cwd);
    if (!r.started || r.exitCode != 0) return false;
    return RStrip(r.stdoutText).rfind("refs/", 0) == 0;
}

std::vector<std::wstring> StripFormatArgs(
    const std::vector<std::wstring>& args) {
    std::vector<std::wstring> out;
    out.reserve(args.size());
    bool pathspecs = false;
    for (const auto& a : args) {
        if (!pathspecs) {
            if (a == L"--") {
                pathspecs = true;
            } else if (a == L"--oneline" || a == L"--pretty" ||
                       StartsWith(a, L"--pretty=") ||
                       StartsWith(a, L"--format=")) {
                continue;
            }
        }
        out.push_back(a);
    }
    return out;
}


}

CommitListResult LoadCommitLog(const std::vector<std::wstring>& logArgs,
                               const std::wstring& cwd) {
    CommitListResult result;

    std::vector<std::wstring> args{
        L"log",
        L"-z",
        L"--pretty=format:%H%x1f%h%x1f%an%x1f%ae%x1f%ad%x1f%B",
        L"--date=format:%Y-%m-%d %H:%M:%S",
    };
    const std::vector<std::wstring> userArgs = StripFormatArgs(logArgs);
    args.insert(args.end(), userArgs.begin(), userArgs.end());

    ProcessResult r = RunGit(args, cwd);
    result.errorMessage = GitFailure(L"git log", r);
    if (!result.errorMessage.empty()) return result;

    for (const std::wstring& record : SplitOn(Utf8ToWide(r.stdoutText), L'\0')) {
        size_t start = record.find_first_not_of(L"\r\n");
        if (start == std::wstring::npos) continue;
        std::vector<std::wstring> fields =
            SplitOn(record.substr(start), L'\x1f');
        if (fields.size() != 6) continue;
        Commit c;
        c.fullSha     = std::move(fields[0]);
        c.shortSha    = std::move(fields[1]);
        c.author      = std::move(fields[2]);
        c.authorEmail = std::move(fields[3]);
        c.date        = std::move(fields[4]);
        c.message     = RStripW(std::move(fields[5]));
        c.subject     = FirstLine(c.message);
        result.commits.push_back(std::move(c));
    }
    return result;
}

std::wstring CurrentBranchLabel(const std::wstring& cwd) {
    std::wstring branch =
        TrimmedOutput(RunGit({L"symbolic-ref", L"--short", L"-q", L"HEAD"}, cwd));
    if (!branch.empty()) return branch;

    std::wstring sha =
        TrimmedOutput(RunGit({L"rev-parse", L"--short", L"HEAD"}, cwd));
    return sha.empty() ? std::wstring() : L"detached at " + sha;
}

std::wstring QueryBranchLabel(const std::vector<std::wstring>& logArgs,
                              const std::wstring& cwd) {
    std::vector<std::wstring> refs;
    for (const auto& a : logArgs) {
        if (a == L"--") break;
        if (a.empty() || a[0] == L'-') continue;
        if (IsRefName(a, cwd)) refs.push_back(a);
    }
    if (refs.size() == 1) return refs.front();
    return CurrentBranchLabel(cwd);
}

CommitListResult LoadCommitRange(const std::wstring& oldSha,
                                 const std::wstring& newSha,
                                 const std::wstring& cwd) {
    return LoadCommitLog(RangeLogArgs(oldSha, newSha), cwd);
}



std::wstring LoadFilesDiff(const std::wstring& sha,
                           const std::vector<std::wstring>& paths,
                           const std::wstring& cwd) {
    if (paths.empty()) return {};
    std::vector<std::wstring> args{
        L"show", L"--format=", L"--no-color", kDiffMerges, sha, L"--",
    };
    args.insert(args.end(), paths.begin(), paths.end());
    ProcessResult r = RunGit(args, cwd);
    if (!r.started || r.exitCode != 0) return {};
    return Utf8ToWide(r.stdoutText);
}

BranchListResult LoadBranchList(const std::wstring& cwd) {
    BranchListResult result;
    ProcessResult r = RunGit({
        L"for-each-ref",
        L"--format=%(HEAD)\x1f%(refname)\x1f%(refname:short)\x1f"
        L"%(upstream:short)\x1f%(objectname:short)\x1f%(subject)",
        L"refs/heads",
        L"refs/remotes",
    }, cwd);
    result.errorMessage = GitFailure(L"git for-each-ref", r);
    if (!result.errorMessage.empty()) return result;
    ForEachLine(Utf8ToWide(r.stdoutText), [&](const std::wstring& line) {
        std::vector<std::wstring> fields = SplitOn(line, L'\x1f');
        if (fields.size() == 6) {
            Branch b;
            b.isCurrent = (!fields[0].empty() && fields[0][0] == L'*');
            const std::wstring& fullRef = fields[1];
            b.isRemote  = (fullRef.compare(0, 13, L"refs/remotes/") == 0);
            b.name      = std::move(fields[2]);
            b.upstream  = std::move(fields[3]);
            b.shortSha  = std::move(fields[4]);
            b.subject   = std::move(fields[5]);
            const bool isHead =
                (b.name == L"HEAD") ||
                (b.name.size() >= 5 &&
                 b.name.compare(b.name.size() - 5, 5, L"/HEAD") == 0);
            if (!isHead) result.branches.push_back(std::move(b));
        }
    });
    return result;
}

ProcessResult CheckoutBranch(const std::wstring& name,
                             const std::wstring& cwd) {
    return RunGit({L"checkout", name}, cwd);
}

std::vector<FileChange> LoadCommitChanges(const std::wstring& sha,
                                          const std::wstring& cwd,
                                          ProcessCanceller* cancel) {
    ProcessResult r = RunGit(
        {L"show", L"--format=", kDiffMerges, L"--name-status", sha},
        cwd, StdioMode::Capture, cancel);
    if (!r.started || r.exitCode != 0) return {};

    std::vector<FileChange> changes = ParseNameStatus(Utf8ToWide(r.stdoutText));

    ProcessResult n = RunGit(
        {L"show", L"--format=", kDiffMerges, L"--numstat", sha},
        cwd, StdioMode::Capture, cancel);
    if (n.started && n.exitCode == 0) {
        auto stats = ParseNumstat(Utf8ToWide(n.stdoutText));
        for (auto& fc : changes) {
            auto it = stats.find(fc.path);
            if (it != stats.end()) {
                fc.insertions = it->second.insertions;
                fc.deletions  = it->second.deletions;
            }
        }
    }
    return changes;
}

}

#include "ui/DiffWindow.hpp"

#include "audio/Audio.hpp"
#include "git/Config.hpp"
#include "ui/DialogUtil.hpp"
#include "ui/FindDialog.hpp"
#include "ui/Shell.hpp"
#include "util/Encoding.hpp"
#include "util/Text.hpp"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace git_tools {

namespace {

constexpr int kIdDiffEdit = 2001;

struct DiffWindowData {
    const DiffWindowParams* params   = nullptr;
    HWND                    hEdit    = nullptr;
    HFONT                   hMono    = nullptr;
    int                     lastLine = -1;

    std::wstring            text;
    std::wstring            folded;
    FindParams              find;

    DiffWindowData() {
        find.wrapAround = ConfigGetBool(kWrapAroundKey, false);
    }
};

void PlayDiffSoundForLine(wchar_t firstChar) {
    if (firstChar == L'+')      PlaySoundResource(L"diffLineInserted");
    else if (firstChar == L'-') PlaySoundResource(L"diffLineDeleted");
}

void CheckCaretLineAndPlay(HWND edit, DiffWindowData* d) {
    const int line = CaretLine(edit);
    if (line == d->lastLine) return;
    d->lastLine = line;

    wchar_t buf[8] = {};
    buf[0] = static_cast<wchar_t>(std::size(buf));
    if (SendMessageW(edit, EM_GETLINE, line, reinterpret_cast<LPARAM>(buf)) >= 1) {
        PlayDiffSoundForLine(buf[0]);
    }
}

const std::wstring& FoldedText(DiffWindowData* d) {
    if (d->folded.size() != d->text.size()) d->folded = ToLower(d->text);
    return d->folded;
}

bool FindInDiff(DiffWindowData* d, bool forward) {
    if (d->find.what.empty()) return false;

    const bool          matchCase = d->find.matchCase;
    const bool          wrap      = d->find.wrapAround;
    const std::wstring& hay       = matchCase ? d->text : FoldedText(d);
    const std::wstring  needle    =
        matchCase ? d->find.what : ToLower(d->find.what);

    size_t pos = std::wstring::npos;
    if (needle.size() <= hay.size()) {
        const auto [selStart, selEnd] = EditSelection(d->hEdit);
        if (forward) {
            pos = hay.find(needle, selEnd);
            if (pos == std::wstring::npos && wrap) pos = hay.find(needle);
        } else {
            if (selStart > 0) pos = hay.rfind(needle, selStart - 1);
            if (pos == std::wstring::npos && wrap) pos = hay.rfind(needle);
        }
    }

    if (pos == std::wstring::npos) {
        MessageBeep(MB_OK);
        return false;
    }

    SendMessageW(d->hEdit, EM_SETSEL, static_cast<WPARAM>(pos),
                 static_cast<LPARAM>(pos + needle.size()));
    SendMessageW(d->hEdit, EM_SCROLLCARET, 0, 0);
    d->lastLine = -1;
    return true;
}

struct DiffLocation {
    std::wstring path;
    int          line = 0;
    std::wstring content;
};

std::vector<std::wstring> SplitLines(std::wstring_view text) {
    std::vector<std::wstring> lines;
    ForEachLine(text, [&](std::wstring_view line) { lines.emplace_back(line); });
    return lines;
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

int MatchLineInFile(const std::vector<std::wstring>& lines,
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
    int value = 0;
    for (wchar_t c : s) {
        if (c < L'0' || c > L'9') break;
        value = value * 10 + (c - L'0');
    }
    return value;
}

bool IsDiffBodyLine(std::wstring_view line, bool includeRemoved) {
    return !line.empty() &&
           (line[0] == L' ' || line[0] == L'+' ||
            (includeRemoved && line[0] == L'-'));
}

DiffLocation LocateInDiff(std::wstring_view text, int caretLine) {
    DiffLocation loc;
    int  index   = 0;
    int  newLine = 0;
    bool inHunk  = false;

    ForEachLine(text, [&](std::wstring_view line) {
        bool content = false;
        if (line.starts_with(L"diff --git ")) {
            inHunk = false;
            const size_t marker = line.rfind(L" b/");
            loc.path = marker == line.npos ? std::wstring()
                                           : std::wstring(line.substr(marker + 3));
        } else if (line.starts_with(L"@@")) {
            if (const size_t plus = line.find(L'+'); plus != line.npos) {
                newLine = LeadingNumber(line.substr(plus + 1));
                inHunk  = true;
            }
        } else if (!inHunk) {
            if (line.starts_with(L"+++ ")) {
                std::wstring_view p = line.substr(4);
                if (p.starts_with(L"b/")) p.remove_prefix(2);
                loc.path = p == L"/dev/null" ? std::wstring() : std::wstring(p);
            }
        } else {
            content = true;
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

void OpenEditorAtCaret(DiffWindowData* d, HWND owner) {
    if (!d->params) return;

    const DiffLocation loc    = LocateInDiff(d->text, CaretLine(d->hEdit));
    const std::wstring editor = FindEditor();
    const std::wstring full   =
        loc.path.empty() ? std::wstring()
                         : RepoFilePath(d->params->workTree, loc.path);

    if (editor.empty() || !PathExists(full)) {
        MessageBeep(MB_OK);
        return;
    }

    int line = loc.line;
    if (!loc.content.empty()) {
        const std::wstring body = Utf8ToWide(ReadFileBytes(full));
        line = body.empty()
                   ? 0
                   : MatchLineInFile(SplitLines(body), loc.content, loc.line);
    }

    if (!OpenWithEditor(owner, editor, full, line)) {
        ShowCouldNotOpen(owner, L"Edit file", full);
    }
}

void OpenFindDialog(DiffWindowData* d, HWND owner) {
    if (ShowFindDialog(owner, d->find)) FindInDiff(d, true);
}

bool IsCaretMove(UINT msg, WPARAM wParam) {
    if (msg == WM_LBUTTONUP) return true;
    if (msg != WM_KEYDOWN) return false;
    switch (wParam) {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
            return true;
        default:
            return false;
    }
}

LRESULT CALLBACK DiffEditSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR dwRefData) {
    auto* d = reinterpret_cast<DiffWindowData*>(dwRefData);

    switch (msg) {
        case WM_GETDLGCODE:
            return (DefSubclassProc(hwnd, msg, wParam, lParam) &
                    ~DLGC_HASSETSEL) | DLGC_WANTTAB;
        case WM_KEYDOWN: {
            if (wParam == VK_TAB) return 0;
            if (!d) break;

            const bool ctrl  = CtrlPressed();
            const bool shift = KeyDown(VK_SHIFT);
            HWND       owner = GetParent(hwnd);

            if (ctrl && wParam == 'A') {
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
                return 0;
            }
            if (ctrl && shift && wParam == 'E') {
                OpenEditorAtCaret(d, owner);
                return 0;
            }
            if (ctrl && wParam == 'F') {
                OpenFindDialog(d, owner);
                return 0;
            }
            if (wParam == VK_F3) {
                if (d->find.what.empty()) OpenFindDialog(d, owner);
                else                      FindInDiff(d, !shift);
                return 0;
            }
            break;
        }
        case WM_CHAR:
            if (wParam == L'\t' || wParam == 0x01 ||
                wParam == 0x05 || wParam == 0x06) {
                return 0;
            }
            break;
    }

    const LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
    if (d && IsCaretMove(msg, wParam)) CheckCaretLineAndPlay(hwnd, d);
    return r;
}

INT_PTR CALLBACK DiffDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<DiffWindowData>(hwnd, msg, lParam);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;

    switch (msg) {
        case WM_INITDIALOG: {
            d->hEdit = CreateReadOnlyEdit(
                hwnd, kIdDiffEdit, WS_HSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN);
            SendMessageW(d->hEdit, EM_SETLIMITTEXT,
                         static_cast<WPARAM>(16 * 1024 * 1024), 0);

            d->hMono = CreateFontW(
                -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            if (d->hMono) {
                SendMessageW(d->hEdit, WM_SETFONT,
                             reinterpret_cast<WPARAM>(d->hMono), TRUE);
            }

            if (d->params) {
                d->text = NormalizeCRLF(d->params->diffText);
                SetReadOnlyText(d->hEdit, d->text);
            }
            SetWindowSubclass(d->hEdit, DiffEditSubclassProc, 1,
                              reinterpret_cast<DWORD_PTR>(d));

            RECT rc;
            GetClientRect(hwnd, &rc);
            MoveWindow(d->hEdit, 0, 0, rc.right, rc.bottom, TRUE);
            SetFocus(d->hEdit);
            return FALSE;
        }
        case WM_SIZE:
            if (d && d->hEdit) {
                MoveWindow(d->hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            }
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wParam) != IDOK) return FALSE;
            EndDialog(hwnd, 0);
            return TRUE;
        case WM_DESTROY:
            if (d && d->hEdit) {
                RemoveWindowSubclass(d->hEdit, DiffEditSubclassProc, 1);
            }
            if (d && d->hMono) {
                DeleteObject(d->hMono);
                d->hMono = nullptr;
            }
            return FALSE;
    }
    return FALSE;
}

}

int ShowDiffWindow(HWND owner, const DiffWindowParams& params) {
    DiffWindowData data;
    data.params = &params;
    const int result = RunDialog(params.title, 600, 480, owner, DiffDlgProc, &data);
    CloseAudio();
    return result;
}

}

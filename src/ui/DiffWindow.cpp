#include "audio/Audio.hpp"
#include "git/Config.hpp"
#include "ui/Shell.hpp"
#include "util/Encoding.hpp"
#include "util/Text.hpp"

#include "ui/DiffWindow.hpp"

#include "ui/App.hpp"
#include "ui/FindDialog.hpp"
#include "ui/Widgets.hpp"

#include <wx/dialog.h>
#include <wx/font.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <algorithm>
#include <vector>

namespace git_tools {

namespace {

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

DiffLocation LocateInDiff(std::wstring_view text, long caretLine) {
    DiffLocation loc;
    long index   = 0;
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

std::wstring StripDiffMarkers(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    size_t width = 0;
    for (size_t pos = 0; pos < text.size();) {
        const size_t      eol  = text.find(L'\n', pos);
        const size_t      next = eol == text.npos ? text.size() : eol + 1;
        std::wstring_view line = text.substr(pos, next - pos);
        pos = next;
        if (line.starts_with(L"diff --git ")) {
            width = 0;
        } else if (line.starts_with(L"@@")) {
            const size_t ats = line.find_first_not_of(L'@');
            width = ats == line.npos ? 0 : ats - 1;
        } else if (width > 0 && line.size() >= width &&
                   line.substr(0, width).find_first_not_of(L" +-") == line.npos) {
            line.remove_prefix(width);
        }
        out += line;
    }
    return out;
}

std::wstring LineMarkers(std::wstring_view text) {
    std::wstring markers;
    ForEachLine(text, [&](std::wstring_view line) {
        markers += line.empty() ? L' ' : line[0];
    });
    return markers;
}

bool IsCaretMoveKey(int key) {
    switch (key) {
        case WXK_UP: case WXK_DOWN: case WXK_LEFT: case WXK_RIGHT:
        case WXK_HOME: case WXK_END: case WXK_PAGEUP: case WXK_PAGEDOWN:
            return true;
        default:
            return false;
    }
}

class DiffDialog : public wxDialog {
public:
    DiffDialog(wxWindow* owner, const DiffWindowParams& params);

private:
    long CaretLine() const;
    void ShowDiffText();
    void ToggleMarkers();
    void CheckCaretLineAndPlay();
    const std::wstring& FoldedText();
    bool FindInDiff(bool forward);
    void OpenFindDialog();
    void OpenEditorAtCaret();
    void OnKeyDown(wxKeyEvent& event);

    const DiffWindowParams& params_;
    wxTextCtrl*             edit_     = nullptr;
    long                    lastLine_ = -1;
    bool                    markers_  = true;
    std::wstring            lineMarkers_;
    std::wstring            text_;
    std::wstring            folded_;
    FindParams              find_;
};

DiffDialog::DiffDialog(wxWindow* owner, const DiffWindowParams& params)
    : wxDialog(owner, wxID_ANY, params.title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMINIMIZE_BOX |
                   wxMAXIMIZE_BOX),
      params_(params) {
    find_.wrapAround = ConfigGetBool(kWrapAroundKey, false);
    markers_         = ConfigGetBool(kDiffMarkersKey, true);
    lineMarkers_     = LineMarkers(params_.diffText);
    PrepareSounds({Sound::LineInserted, Sound::LineDeleted});

    edit_ = CreateReadOnlyText(this, wxTE_DONTWRAP | wxHSCROLL | wxTE_PROCESS_TAB);
    wxFontInfo font(10.5);
    font.Family(wxFONTFAMILY_TELETYPE);
#ifdef _WIN32
    font.FaceName(L"Consolas");
#endif
    edit_->SetFont(wxFont(font));
    ShowDiffText();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(edit_, 1, wxEXPAND);
    SetSizer(sizer);
    SetSize(FromDIP(wxSize(1030, 780)));
    CentreOnParent();

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE &&
            event.GetModifiers() == wxMOD_NONE) {
            EndModal(wxID_CANCEL);
            return;
        }
        event.Skip();
    });
    edit_->Bind(wxEVT_KEY_DOWN, &DiffDialog::OnKeyDown, this);
    edit_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        event.Skip();
        CallAfter([this] { CheckCaretLineAndPlay(); });
    });
    edit_->SetFocus();
}

long DiffDialog::CaretLine() const {
    long column = 0;
    long line   = 0;
    return edit_->PositionToXY(edit_->GetInsertionPoint(), &column, &line)
               ? line
               : -1;
}

void DiffDialog::ShowDiffText() {
    text_ = markers_ ? NativeLineEnds(params_.diffText)
                     : NativeLineEnds(StripDiffMarkers(params_.diffText));
    folded_.clear();
    SetReadOnlyText(edit_, text_);
}

void DiffDialog::ToggleMarkers() {
    long column = 0;
    long line   = 0;
    const bool located =
        edit_->PositionToXY(edit_->GetInsertionPoint(), &column, &line);
    const long before = located ? edit_->GetLineLength(line) : 0;

    markers_ = !markers_;
    ConfigSetBool(kDiffMarkersKey, markers_);
    ShowDiffText();
    if (!located) return;

    const long length = edit_->GetLineLength(line);
    const long delta  = before - length;
    const long start  = edit_->XYToPosition(0, line);
    if (start < 0) return;
    const long target = start + std::clamp(column - delta, 0L, std::max(length, 0L));
    edit_->SetInsertionPoint(target);
    edit_->ShowPosition(target);
}

void DiffDialog::CheckCaretLineAndPlay() {
    const long line = CaretLine();
    if (line < 0 || line == lastLine_) return;
    lastLine_ = line;

    if (static_cast<size_t>(line) >= lineMarkers_.size()) return;
    const wchar_t marker = lineMarkers_[static_cast<size_t>(line)];
    if (marker == L'+') {
        PlaySoundEffect(Sound::LineInserted);
    } else if (marker == L'-') {
        PlaySoundEffect(Sound::LineDeleted);
    }
}

const std::wstring& DiffDialog::FoldedText() {
    if (folded_.size() != text_.size()) folded_ = ToLower(text_);
    return folded_;
}

bool DiffDialog::FindInDiff(bool forward) {
    if (find_.what.empty()) return false;

    const bool          matchCase = find_.matchCase;
    const bool          wrap      = find_.wrapAround;
    const std::wstring& hay       = matchCase ? text_ : FoldedText();
    const std::wstring  needle    = matchCase ? find_.what : ToLower(find_.what);

    size_t pos = std::wstring::npos;
    if (needle.size() <= hay.size()) {
        long selStart = 0;
        long selEnd   = 0;
        edit_->GetSelection(&selStart, &selEnd);
        if (forward) {
            pos = hay.find(needle, static_cast<size_t>(selEnd));
            if (pos == std::wstring::npos && wrap) pos = hay.find(needle);
        } else {
            if (selStart > 0) pos = hay.rfind(needle, static_cast<size_t>(selStart - 1));
            if (pos == std::wstring::npos && wrap) pos = hay.rfind(needle);
        }
    }

    if (pos == std::wstring::npos) {
        wxBell();
        return false;
    }

    edit_->SetSelection(static_cast<long>(pos),
                        static_cast<long>(pos + needle.size()));
    lastLine_ = -1;
    return true;
}

void DiffDialog::OpenFindDialog() {
    if (ShowFindDialog(this, find_)) FindInDiff(true);
}

void DiffDialog::OpenEditorAtCaret() {
    const DiffLocation loc    = LocateInDiff(params_.diffText, CaretLine());
    const std::wstring editor = FindEditor();
    const std::wstring full   =
        loc.path.empty() ? std::wstring() : RepoFilePath(params_.workTree, loc.path);

    if (editor.empty() || !PathExists(full)) {
        wxBell();
        return;
    }

    int line = loc.line;
    if (!loc.content.empty()) {
        const std::wstring body = Utf8ToWide(ReadFileBytes(full));
        line = body.empty()
                   ? 0
                   : MatchLineInFile(SplitLines(body), loc.content, loc.line);
    }

    if (!OpenWithEditor(editor, full, line)) {
        ShowCouldNotOpen(this, L"Edit file", full);
    }
}

void DiffDialog::OnKeyDown(wxKeyEvent& event) {
    const int  key       = event.GetKeyCode();
    const int  modifiers = event.GetModifiers();
    const bool ctrl      = (modifiers & ~wxMOD_SHIFT) == wxMOD_CONTROL;
    const bool shift     = (modifiers & wxMOD_SHIFT) != 0;

    if (key == WXK_TAB) return;
    if (ctrl && !shift && key == 'A') {
        edit_->SelectAll();
        return;
    }
    if (ctrl && shift && key == 'E') {
        OpenEditorAtCaret();
        return;
    }
    if (ctrl && !shift && key == 'I') {
        ToggleMarkers();
        return;
    }
    if (ctrl && !shift && key == 'F') {
        OpenFindDialog();
        return;
    }
    if (key == WXK_F3 && (modifiers & ~wxMOD_SHIFT) == wxMOD_NONE) {
        if (find_.what.empty()) OpenFindDialog();
        else                    FindInDiff(!shift);
        return;
    }

    event.Skip();
    if (IsCaretMoveKey(key)) CallAfter([this] { CheckCaretLineAndPlay(); });
}

}

void ShowDiffWindow(wxWindow* owner, const DiffWindowParams& params) {
    {
        DiffDialog dialog(owner, params);
        dialog.ShowModal();
    }
    CloseAudio();
}

}

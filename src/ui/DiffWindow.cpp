#include "audio/Audio.hpp"
#include "git/Config.hpp"
#include "git/Diff.hpp"
#include "ui/Shell.hpp"
#include "util/Encoding.hpp"
#include "util/Text.hpp"

#include "ui/DiffWindow.hpp"

#include "ui/Announce.hpp"
#include "ui/App.hpp"
#include "ui/FindDialog.hpp"
#include "ui/MenuFrame.hpp"
#include "ui/Widgets.hpp"

#include <wx/font.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <algorithm>
#include <vector>

namespace git_tools {

namespace {

bool IsCaretMoveKey(int key) {
    switch (key) {
        case WXK_UP: case WXK_DOWN: case WXK_LEFT: case WXK_RIGHT:
        case WXK_HOME: case WXK_END: case WXK_PAGEUP: case WXK_PAGEDOWN:
            return true;
        default:
            return false;
    }
}

struct DisplayLine {
    size_t source = 0;
    size_t start  = 0;
};

struct SourcePos {
    size_t line   = 0;
    size_t column = 0;
};

class DiffFrame : public MenuFrame {
public:
    DiffFrame(wxWindow* owner, const DiffWindowParams& params);
    ~DiffFrame() override;

protected:
    std::vector<MenuSection> Menus() override;
    void                     OnOptionsChanged() override;

private:
    bool   CaretXY(long& column, long& line) const;
    long   CaretLine() const;
    bool   CaretSource(SourcePos& pos) const;
    size_t Hidden(size_t line) const;
    bool   ToSource(long position, SourcePos& pos) const;
    long   FromSource(const SourcePos& pos) const;
    size_t SourceColumn(const SourcePos& pos, bool withMarkers) const;
    bool   HasSelection() const;
    MenuEntries CopyEntries();
    MenuEntries EditorEntries();
    void ShowDiffText();
    void Rerender(bool markers, size_t wrapWidth);
    void ToggleMarkers();
    void GoHome();
    void CheckCaretLineAndPlay();
    const std::wstring& FoldedText();
    bool FindInDiff(bool forward);
    void FindAgain(bool forward);
    void OpenFindDialog();
    bool EditableFileAtCaret(DiffLocation& loc, std::wstring& path) const;
    void OpenEditor(bool atLine);
    void CopyText(bool withMarkers);
    void ShowContextMenu(wxPoint at);
    void OnKeyDown(wxKeyEvent& event);

    std::wstring              diff_;
    std::wstring              workTree_;
    std::vector<FileDiff>     files_;
    wxTextCtrl*               edit_      = nullptr;
    long                      lastLine_  = -1;
    bool                      markers_   = true;
    size_t                    wrapWidth_ = 0;
    std::vector<std::wstring> source_;
    std::vector<size_t>       markerWidths_;
    std::vector<DisplayLine>  layout_;
    std::wstring              text_;
    std::wstring              folded_;
    FindParams                find_;
};

DiffFrame::DiffFrame(wxWindow* owner, const DiffWindowParams& params)
    : MenuFrame(owner, params.title, wxSize(1030, 780)),
      diff_(SeparateFileDiffs(params.diffText)),
      workTree_(params.workTree),
      files_(SplitFileDiffs(diff_)) {
    find_.wrapAround = ConfigGetBool(kWrapAroundKey, false);
    markers_         = ConfigGetBool(kDiffMarkersKey, true);
    wrapWidth_       = static_cast<size_t>(LineWrapWidth());
    source_          = SplitLines(NormalizeLF(diff_));
    markerWidths_    = MarkerWidths(source_);
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

    BuildMenus();
    PrepareAnnouncements(this);
    edit_->Bind(wxEVT_KEY_DOWN, &DiffFrame::OnKeyDown, this);
    edit_->Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent& event) {
        ShowContextMenu(event.GetPosition());
    });
    edit_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        event.Skip();
        CallAfter([this] { CheckCaretLineAndPlay(); });
    });
    edit_->SetFocus();
}

DiffFrame::~DiffFrame() {
    CloseAudio();
}

std::vector<MenuSection> DiffFrame::Menus() {
    return {
        {L"&File", WithFileCommands(EditorEntries())},
        {L"&Edit", AppendGroup(CopyEntries(), {
            {L"&Find...\tCtrl+F", [this] { OpenFindDialog(); }},
            {L"Find &next\tF3", [this] { FindAgain(true); }},
            {L"Find &previous\tShift+F3", [this] { FindAgain(false); }},
        })},
        {L"&View", {
            {L"Show + and - &markers\tCtrl+I", [this] { ToggleMarkers(); }, true, true,
             markers_},
        }},
    };
}

void DiffFrame::OnOptionsChanged() {
    const bool   markers = ConfigGetBool(kDiffMarkersKey, true);
    const size_t wrap    = static_cast<size_t>(LineWrapWidth());
    if (markers != markers_ || wrap != wrapWidth_) Rerender(markers, wrap);
}

bool DiffFrame::HasSelection() const {
    const auto [from, to] = TextSelection(edit_);
    return from != to;
}

MenuEntries DiffFrame::CopyEntries() {
    return {
        {L"&Copy\tCtrl+C", [this] { CopyText(markers_); }, HasSelection()},
        {L"Copy with &markers\tCtrl+Shift+C", [this] { CopyText(true); }},
        {L"Select &all\tCtrl+A", [this] { edit_->SelectAll(); }},
    };
}

MenuEntries DiffFrame::EditorEntries() {
    DiffLocation loc;
    std::wstring path;
    const bool   canEdit = EditableFileAtCaret(loc, path);
    return {
        {L"Open &file in editor\tCtrl+E", [this] { OpenEditor(false); }, canEdit},
        {L"Open editor on current &line\tCtrl+Shift+E", [this] { OpenEditor(true); },
         canEdit},
    };
}

bool DiffFrame::CaretXY(long& column, long& line) const {
    return edit_->PositionToXY(edit_->GetInsertionPoint(), &column, &line);
}

long DiffFrame::CaretLine() const {
    long column = 0;
    long line   = 0;
    return CaretXY(column, line) ? line : -1;
}

bool DiffFrame::CaretSource(SourcePos& pos) const {
    return ToSource(edit_->GetInsertionPoint(), pos);
}

size_t DiffFrame::Hidden(size_t line) const {
    return markers_ || line >= markerWidths_.size() ? 0 : markerWidths_[line];
}

bool DiffFrame::ToSource(long position, SourcePos& pos) const {
    long column = 0;
    long line   = 0;
    if (!edit_->PositionToXY(position, &column, &line) || line < 0 ||
        static_cast<size_t>(line) >= layout_.size()) {
        return false;
    }
    const DisplayLine& shown = layout_[static_cast<size_t>(line)];
    pos = {shown.source, shown.start + static_cast<size_t>(std::max(column, 0L))};
    return true;
}

long DiffFrame::FromSource(const SourcePos& pos) const {
    auto it = std::ranges::lower_bound(layout_, pos.line, {}, &DisplayLine::source);
    if (it == layout_.end() || it->source != pos.line) return -1;
    while (it + 1 != layout_.end() && (it + 1)->source == pos.line &&
           (it + 1)->start <= pos.column) {
        ++it;
    }
    const long line  = static_cast<long>(it - layout_.begin());
    const long start = edit_->XYToPosition(0, line);
    if (start < 0) return -1;
    const long length = std::max<long>(edit_->GetLineLength(line), 0);
    return start + std::clamp(static_cast<long>(pos.column - it->start), 0L, length);
}

size_t DiffFrame::SourceColumn(const SourcePos& pos, bool withMarkers) const {
    return withMarkers && pos.column == 0 ? 0 : pos.column + Hidden(pos.line);
}

void DiffFrame::ShowDiffText() {
    std::wstring text;
    layout_.clear();
    for (size_t i = 0; i < source_.size(); ++i) {
        const std::wstring_view line =
            std::wstring_view(source_[i]).substr(std::min(Hidden(i), source_[i].size()));
        const std::vector<size_t> starts = WrapPoints(line, wrapWidth_);
        for (size_t s = 0; s < starts.size(); ++s) {
            const size_t end = s + 1 < starts.size() ? starts[s + 1] : line.size();
            if (!layout_.empty()) text += L'\n';
            text += line.substr(starts[s], end - starts[s]);
            layout_.push_back({i, starts[s]});
        }
    }
    text_ = NativeLineEnds(text);
    folded_.clear();
    SetReadOnlyText(edit_, text_);
}

void DiffFrame::Rerender(bool markers, size_t wrapWidth) {
    SourcePos    pos;
    const bool   located = CaretSource(pos);
    const size_t full    = located ? SourceColumn(pos, false) : 0;

    markers_   = markers;
    wrapWidth_ = wrapWidth;
    ShowDiffText();

    if (located) {
        pos.column = full - std::min(full, Hidden(pos.line));
        if (const long at = FromSource(pos); at >= 0) MoveCaret(edit_, at);
    }
}

void DiffFrame::ToggleMarkers() {
    ConfigSetBool(kDiffMarkersKey, !markers_);
    Rerender(!markers_, wrapWidth_);
    Announce(edit_, markers_ ? L"Diff markers shown" : L"Diff markers hidden");
}

void DiffFrame::GoHome() {
    long column = 0;
    long line   = 0;
    if (!CaretXY(column, line)) return;
    const long start = edit_->XYToPosition(0, line);
    if (start < 0) return;

    size_t target = 0;
    if (column == 0) {
        const std::wstring text = edit_->GetLineText(line).ToStdWstring();
        while (target < text.size() && (text[target] == L' ' || text[target] == L'\t')) {
            ++target;
        }
    }
    MoveCaret(edit_, start + static_cast<long>(target));
}

void DiffFrame::CheckCaretLineAndPlay() {
    const long line = CaretLine();
    if (line < 0 || line == lastLine_) return;
    lastLine_ = line;

    if (static_cast<size_t>(line) >= layout_.size()) return;
    const std::wstring& source = source_[layout_[static_cast<size_t>(line)].source];
    const wchar_t marker = source.empty() ? L' ' : source.front();
    if (marker == L'+') {
        PlaySoundEffect(Sound::LineInserted);
    } else if (marker == L'-') {
        PlaySoundEffect(Sound::LineDeleted);
    }
}

const std::wstring& DiffFrame::FoldedText() {
    if (folded_.size() != text_.size()) folded_ = ToLower(text_);
    return folded_;
}

bool DiffFrame::FindInDiff(bool forward) {
    if (find_.what.empty()) return false;

    const bool          matchCase = find_.matchCase;
    const bool          wrap      = find_.wrapAround;
    const std::wstring& hay       = matchCase ? text_ : FoldedText();
    const std::wstring  needle    = matchCase ? find_.what : ToLower(find_.what);

    size_t pos = std::wstring::npos;
    if (needle.size() <= hay.size()) {
        const auto [selStart, selEnd] = TextSelection(edit_);
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

void DiffFrame::FindAgain(bool forward) {
    if (find_.what.empty()) OpenFindDialog();
    else                    FindInDiff(forward);
}

void DiffFrame::OpenFindDialog() {
    if (ShowFindDialog(this, find_)) FindInDiff(true);
}

bool DiffFrame::EditableFileAtCaret(DiffLocation& loc, std::wstring& path) const {
    SourcePos pos;
    if (FindEditor().empty() || !CaretSource(pos)) return false;
    loc  = LocateInDiff(diff_, files_, pos.line);
    path = loc.path.empty() ? std::wstring() : RepoFilePath(workTree_, loc.path);
    return PathExists(path);
}

void DiffFrame::OpenEditor(bool atLine) {
    DiffLocation loc;
    std::wstring full;
    if (!EditableFileAtCaret(loc, full)) {
        wxBell();
        return;
    }

    EditFile(this, full, atLine ? LineInFile(loc, full) : 0);
}

void DiffFrame::CopyText(bool withMarkers) {
    const auto [from, to] = TextSelection(edit_);
    if (from == to) {
        if (withMarkers) SetClipboardText(NativeLineEnds(diff_));
        return;
    }

    SourcePos first;
    SourcePos last;
    if (!ToSource(from, first) || !ToSource(to, last)) return;

    std::wstring out;
    for (size_t line = first.line; line <= last.line && line < source_.size(); ++line) {
        const std::wstring& text  = source_[line];
        size_t              begin = line == first.line ? SourceColumn(first, withMarkers)
                                    : withMarkers      ? 0
                                                       : Hidden(line);
        size_t end = line == last.line ? SourceColumn(last, withMarkers) : text.size();
        begin = std::min(begin, text.size());
        end   = std::clamp(end, begin, text.size());
        if (line != first.line) out += L'\n';
        out.append(text, begin, end - begin);
    }
    SetClipboardText(NativeLineEnds(out));
}

void DiffFrame::ShowContextMenu(wxPoint at) {
    if (at == wxDefaultPosition) {
        at = edit_->PositionToCoords(edit_->GetInsertionPoint());
        at = at == wxDefaultPosition ? wxPoint(0, 0)
                                     : wxPoint(at.x, at.y + edit_->GetCharHeight());
    } else {
        at = edit_->ScreenToClient(at);
    }

    ShowPopupMenu(edit_, at, AppendGroup(CopyEntries(), EditorEntries()));
}

void DiffFrame::OnKeyDown(wxKeyEvent& event) {
    const int key = event.GetKeyCode();
    if (key == WXK_TAB || HandleShortcut(event)) return;
    if (IsKey(event, WXK_HOME)) {
        GoHome();
        return;
    }
    event.Skip();
    if (IsCaretMoveKey(key)) CallAfter([this] { CheckCaretLineAndPlay(); });
}

}

void ShowDiffWindow(wxWindow* owner, const DiffWindowParams& params) {
    auto* frame = new DiffFrame(owner, params);
    if (!owner) {
        ShowOnActiveDisplay(frame);
        return;
    }
    frame->CentreOnParent();
    frame->RunModal();
}

}

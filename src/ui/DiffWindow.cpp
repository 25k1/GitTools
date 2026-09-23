#include "ui/DiffWindow.hpp"

#include "git/Config.hpp"
#include "ui/DialogUtil.hpp"
#include "ui/FindDialog.hpp"
#include "ui/Shell.hpp"

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>

#include "ui/Encoding.hpp"

#include <algorithm>
#include <cstring>
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
        find.wrapAround = ConfigGetBool(L"wraparound", false);
    }
};

struct WavInfo {
    uint16_t audioFormat   = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataOffset    = 0;
    uint32_t dataSize      = 0;
};

WavInfo ParseWav(const uint8_t* data, size_t size) {
    WavInfo info;
    if (size < 12) return info;
    if (memcmp(data,     "RIFF", 4) != 0) return info;
    if (memcmp(data + 8, "WAVE", 4) != 0) return info;

    size_t pos = 12;
    while (pos + 8 <= size) {
        uint32_t chunkSize = 0;
        memcpy(&chunkSize, data + pos + 4, 4);
        const size_t chunkStart = pos + 8;
        if (chunkStart > size) break;

        if (memcmp(data + pos, "fmt ", 4) == 0 && chunkStart + 16 <= size) {
            memcpy(&info.audioFormat,   data + chunkStart + 0,  2);
            memcpy(&info.bitsPerSample, data + chunkStart + 14, 2);
        } else if (memcmp(data + pos, "data", 4) == 0) {
            info.dataOffset = static_cast<uint32_t>(chunkStart);
            info.dataSize   = chunkSize;
            break;
        }
        pos = chunkStart + chunkSize;
        if (chunkSize & 1) ++pos;
    }
    return info;
}

float& VolumeCache() {
    static float v = -1.0f;
    return v;
}

bool& SoundsLoaded() {
    static bool loaded = false;
    return loaded;
}

float SoundVolume() {
    float& v = VolumeCache();
    if (v < 0.0f) {
        int pct = ConfigGetInt(L"soundvolume", 50);
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        v = static_cast<float>(pct) / 100.0f;
    }
    return v;
}

void ScaleWavVolume(uint8_t* data, size_t size, float factor) {
    WavInfo info = ParseWav(data, size);
    if (info.dataOffset == 0 || info.dataSize == 0) return;
    if (info.dataOffset + info.dataSize > size) {
        info.dataSize = static_cast<uint32_t>(size - info.dataOffset);
    }
    uint8_t* samples = data + info.dataOffset;
    const uint32_t bytes = info.dataSize;

    if (info.audioFormat == 1) {
        if (info.bitsPerSample == 16) {
            auto* s = reinterpret_cast<int16_t*>(samples);
            const size_t n = bytes / 2;
            for (size_t i = 0; i < n; ++i) {
                s[i] = static_cast<int16_t>(s[i] * factor);
            }
        } else if (info.bitsPerSample == 8) {
            for (size_t i = 0; i < bytes; ++i) {
                int v = static_cast<int>(samples[i]) - 128;
                v = static_cast<int>(v * factor);
                samples[i] = static_cast<uint8_t>(v + 128);
            }
        } else if (info.bitsPerSample == 24) {
            for (size_t i = 0; i + 3 <= bytes; i += 3) {
                int32_t v = static_cast<int32_t>(samples[i])
                          | (static_cast<int32_t>(samples[i + 1]) << 8)
                          | (static_cast<int32_t>(
                                static_cast<int8_t>(samples[i + 2])) << 16);
                v = static_cast<int32_t>(v * factor);
                samples[i]     = static_cast<uint8_t>( v        & 0xFF);
                samples[i + 1] = static_cast<uint8_t>((v >> 8)  & 0xFF);
                samples[i + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
            }
        }
    } else if (info.audioFormat == 3 && info.bitsPerSample == 32) {
        auto* s = reinterpret_cast<float*>(samples);
        const size_t n = bytes / 4;
        for (size_t i = 0; i < n; ++i) s[i] *= factor;
    }
}

std::vector<uint8_t>& InsertedSoundBuffer() {
    static std::vector<uint8_t> b; return b;
}
std::vector<uint8_t>& DeletedSoundBuffer() {
    static std::vector<uint8_t> b; return b;
}

void LoadAndProcessSound(LPCWSTR name, std::vector<uint8_t>& dest) {
    HMODULE hMod = GetModuleHandleW(nullptr);
    HRSRC hRes = FindResourceW(hMod, name, L"WAVE");
    if (!hRes) return;
    HGLOBAL hData = LoadResource(hMod, hRes);
    if (!hData) return;
    LPCVOID p = LockResource(hData);
    if (!p) return;
    DWORD size = SizeofResource(hMod, hRes);
    const auto* src = reinterpret_cast<const uint8_t*>(p);
    dest.assign(src, src + size);
    const float volume = SoundVolume();
    if (volume < 1.0f) ScaleWavVolume(dest.data(), dest.size(), volume);
}

void EnsureSoundsLoaded() {
    if (SoundsLoaded()) return;
    SoundsLoaded() = true;
    LoadAndProcessSound(L"diffLineInserted", InsertedSoundBuffer());
    LoadAndProcessSound(L"diffLineDeleted",  DeletedSoundBuffer());
}

void PlayDiffSoundForLine(wchar_t firstChar) {
    if (SoundVolume() <= 0.0f) return;
    EnsureSoundsLoaded();
    const std::vector<uint8_t>* buf = nullptr;
    if (firstChar == L'+')      buf = &InsertedSoundBuffer();
    else if (firstChar == L'-') buf = &DeletedSoundBuffer();
    else return;
    if (buf->empty()) return;
    PlaySoundW(reinterpret_cast<LPCWSTR>(buf->data()), nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void CheckCaretLineAndPlay(HWND hEdit, DiffWindowData* d) {
    DWORD startCh = 0, endCh = 0;
    SendMessageW(hEdit, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&startCh),
                 reinterpret_cast<LPARAM>(&endCh));
    int line = static_cast<int>(
        SendMessageW(hEdit, EM_LINEFROMCHAR, startCh, 0));
    if (line == d->lastLine) return;
    d->lastLine = line;

    wchar_t buf[8] = {};
    *reinterpret_cast<WORD*>(buf) =
        static_cast<WORD>(sizeof(buf) / sizeof(wchar_t));
    int got = static_cast<int>(
        SendMessageW(hEdit, EM_GETLINE, line,
                     reinterpret_cast<LPARAM>(buf)));
    if (got >= 1) PlayDiffSoundForLine(buf[0]);
}

const std::wstring& FoldedText(DiffWindowData* d) {
    if (d->folded.size() != d->text.size()) {
        d->folded = d->text;
        if (!d->folded.empty()) {
            CharLowerBuffW(d->folded.data(),
                           static_cast<DWORD>(d->folded.size()));
        }
    }
    return d->folded;
}

bool FindInDiff(DiffWindowData* d, bool forward) {
    if (d->find.what.empty()) return false;

    const bool matchCase = d->find.matchCase;
    const std::wstring& hay = matchCase ? d->text : FoldedText(d);

    std::wstring needle = d->find.what;
    if (!matchCase) {
        CharLowerBuffW(needle.data(), static_cast<DWORD>(needle.size()));
    }

    size_t pos = std::wstring::npos;
    if (needle.size() <= hay.size()) {
        DWORD selStart = 0, selEnd = 0;
        SendMessageW(d->hEdit, EM_GETSEL,
                     reinterpret_cast<WPARAM>(&selStart),
                     reinterpret_cast<LPARAM>(&selEnd));
        if (forward) {
            pos = hay.find(needle, selEnd);
            if (pos == std::wstring::npos && d->find.wrapAround) {
                pos = hay.find(needle);
            }
        } else {
            if (selStart > 0) pos = hay.rfind(needle, selStart - 1);
            if (pos == std::wstring::npos && d->find.wrapAround) {
                pos = hay.rfind(needle);
            }
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

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t pos = 0;
    for (;;) {
        size_t eol = text.find(L'\n', pos);
        size_t end = (eol == std::wstring::npos) ? text.size() : eol;
        std::wstring line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        if (eol == std::wstring::npos) break;
        pos = eol + 1;
    }
    return lines;
}

double Similarity(const std::wstring& a, const std::wstring& b) {
    const size_t n = (a.size() < 256) ? a.size() : 256;
    const size_t m = (b.size() < 256) ? b.size() : 256;
    if (n == 0 || m == 0) return (n == m) ? 1.0 : 0.0;

    std::vector<int> prev(m + 1);
    std::vector<int> cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int best = prev[j] + 1;
            if (cur[j - 1] + 1 < best)    best = cur[j - 1] + 1;
            if (prev[j - 1] + cost < best) best = prev[j - 1] + cost;
            cur[j] = best;
        }
        prev.swap(cur);
    }
    const double longest = static_cast<double>((n > m) ? n : m);
    return 1.0 - static_cast<double>(prev[m]) / longest;
}

int MatchLineInFile(const std::vector<std::wstring>& lines,
                    const std::wstring& needle, int estimate) {
    if (lines.empty() || needle.empty()) return 0;

    const int count = static_cast<int>(lines.size());
    int center = (estimate > 0 ? estimate : 1) - 1;
    if (center >= count) center = count - 1;

    for (int step = 0; step < count; ++step) {
        const int before = center - step;
        const int after  = center + step;
        if (before >= 0 && lines[before] == needle) return before + 1;
        if (after != before && after < count && lines[after] == needle) {
            return after + 1;
        }
        if (before < 0 && after >= count) break;
    }

    constexpr int kWindow = 300;
    constexpr double kMinScore = 0.6;
    double best = 0.0;
    int    bestLine = 0;
    for (int step = 0; step <= kWindow; ++step) {
        for (int side = 0; side < 2; ++side) {
            if (step == 0 && side == 1) continue;
            const int i = side ? center + step : center - step;
            if (i < 0 || i >= count) continue;
            const double score = Similarity(lines[i], needle);
            if (score > best) {
                best = score;
                bestLine = i + 1;
            }
        }
    }
    return (best >= kMinScore) ? bestLine : 0;
}


DiffLocation LocateInDiff(const std::wstring& text, int caretLine) {
    DiffLocation loc;
    int  index   = 0;
    int  newLine = 0;
    bool inHunk  = false;

    size_t pos = 0;
    for (;;) {
        size_t eol = text.find(L'\n', pos);
        size_t end = (eol == std::wstring::npos) ? text.size() : eol;
        std::wstring line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        bool content = false;
        if (line.rfind(L"diff --git ", 0) == 0) {
            inHunk = false;
            size_t marker = line.rfind(L" b/");
            loc.path = (marker == std::wstring::npos)
                           ? std::wstring()
                           : line.substr(marker + 3);
        } else if (line.rfind(L"@@", 0) == 0) {
            size_t plus = line.find(L'+');
            if (plus != std::wstring::npos) {
                int start = 0;
                for (size_t i = plus + 1;
                     i < line.size() && line[i] >= L'0' && line[i] <= L'9';
                     ++i) {
                    start = start * 10 + (line[i] - L'0');
                }
                newLine = start;
                inHunk  = true;
            }
        } else if (!inHunk) {
            if (line.rfind(L"+++ ", 0) == 0) {
                std::wstring p = line.substr(4);
                if (p.rfind(L"b/", 0) == 0) p = p.substr(2);
                loc.path = (p == L"/dev/null") ? std::wstring() : p;
            }
        } else {
            content = true;
        }

        if (index == caretLine) {
            if (inHunk) {
                loc.line = newLine;
                if (!line.empty() &&
                    (line[0] == L' ' || line[0] == L'+' || line[0] == L'-')) {
                    loc.content = line.substr(1);
                }
            }
            break;
        }
        if (content && (line.empty() || line[0] == L' ' || line[0] == L'+')) {
            ++newLine;
        }

        if (eol == std::wstring::npos) break;
        pos = eol + 1;
        ++index;
    }
    return loc;
}

void OpenEditorAtCaret(DiffWindowData* d, HWND owner) {
    if (!d->params) return;

    DWORD selStart = 0, selEnd = 0;
    SendMessageW(d->hEdit, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&selStart),
                 reinterpret_cast<LPARAM>(&selEnd));
    const int caretLine = static_cast<int>(
        SendMessageW(d->hEdit, EM_LINEFROMCHAR, selStart, 0));

    const DiffLocation loc = LocateInDiff(d->text, caretLine);
    const std::wstring editor = FindEditor();
    const std::wstring full =
        loc.path.empty() ? std::wstring()
                         : RepoFilePath(d->params->workTree, loc.path);

    if (editor.empty() || full.empty() || !PathExists(full)) {
        MessageBeep(MB_OK);
        return;
    }

    int line = 0;
    if (!loc.content.empty()) {
        const std::wstring body = Utf8ToWide(ReadFileBytes(full));
        if (!body.empty()) {
            line = MatchLineInFile(SplitLines(body), loc.content, loc.line);
        }
    } else if (loc.line > 0) {
        line = loc.line;
    }

    if (!OpenWithEditor(owner, editor, full, line)) {
        ShowError(owner, L"Edit file", L"Could not open:\n\n" + full);
    }
}

void OpenFindDialog(DiffWindowData* d, HWND owner) {
    if (ShowFindDialog(owner, d->find)) FindInDiff(d, true);
}

LRESULT CALLBACK DiffEditSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR dwRefData) {
    auto* d = reinterpret_cast<DiffWindowData*>(dwRefData);

    switch (msg) {
        case WM_GETDLGCODE: {
            LRESULT base = DefSubclassProc(hwnd, msg, wParam, lParam);
            return (base & ~DLGC_HASSETSEL) | DLGC_WANTTAB;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_TAB) return 0;
            if (!d) break;

            const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                               (GetKeyState(VK_MENU)    & 0x8000) == 0;
            const bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

            if (ctrl && wParam == 'A') {
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
                return 0;
            }
            if (ctrl && shift && wParam == 'E') {
                OpenEditorAtCaret(d, GetParent(hwnd));
                return 0;
            }
            if (ctrl && wParam == 'F') {
                OpenFindDialog(d, GetParent(hwnd));
                return 0;
            }
            if (wParam == VK_F3) {
                if (d->find.what.empty()) OpenFindDialog(d, GetParent(hwnd));
                else FindInDiff(d, !shift);
                return 0;
            }
            break;
        }
        case WM_CHAR:
            if (wParam == L'\t' || wParam == 0x01 ||
                wParam == 0x05 || wParam == 0x06) return 0;
            break;
    }

    LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
    if (!d) return r;

    bool maybeMoved = false;
    if (msg == WM_KEYDOWN) {
        switch (wParam) {
            case VK_UP: case VK_DOWN:
            case VK_LEFT: case VK_RIGHT:
            case VK_HOME: case VK_END:
            case VK_PRIOR: case VK_NEXT:
                maybeMoved = true;
                break;
        }
    } else if (msg == WM_LBUTTONUP) {
        maybeMoved = true;
    }
    if (maybeMoved) CheckCaretLineAndPlay(hwnd, d);
    return r;
}

INT_PTR CALLBACK DiffDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* d = DialogState<DiffWindowData>(hwnd);

    if (HandleDialogClose(hwnd, msg, wParam)) return TRUE;

    switch (msg) {
        case WM_INITDIALOG: {
            d = AttachDialogState<DiffWindowData>(hwnd, lParam);

            d->hEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE |
                    WS_VSCROLL | WS_HSCROLL |
                    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                    ES_AUTOHSCROLL | ES_NOHIDESEL | ES_WANTRETURN,
                0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDiffEdit)),
                GetModuleHandleW(nullptr), nullptr);

            SendMessageW(d->hEdit, EM_SETLIMITTEXT,
                         static_cast<WPARAM>(16 * 1024 * 1024), 0);

            d->hMono = CreateFontW(
                -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                L"Consolas");
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
                MoveWindow(d->hEdit, 0, 0,
                           LOWORD(lParam), HIWORD(lParam), TRUE);
            }
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                EndDialog(hwnd, 0);
                return TRUE;
            }
            return FALSE;
        case WM_DESTROY:
            if (d) {
                if (d->hEdit) {
                    RemoveWindowSubclass(d->hEdit, DiffEditSubclassProc, 1);
                }
                if (d->hMono) {
                    DeleteObject(d->hMono);
                    d->hMono = nullptr;
                }
            }
            return FALSE;
    }
    return FALSE;
}

}

void ResetSoundCache() {
    PlaySoundW(nullptr, nullptr, 0);
    SoundsLoaded() = false;
    VolumeCache()  = -1.0f;
    InsertedSoundBuffer().clear();
    DeletedSoundBuffer().clear();
}

int ShowDiffWindow(HWND owner, const DiffWindowParams& params) {
    DiffWindowData data;
    data.params = &params;
    return RunDialog(params.title, 600, 480, owner, DiffDlgProc, &data);
}

}

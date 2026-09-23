#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace git_tools {

struct DlgTemplateBuilder {
    std::vector<uint8_t> buf;
    void AlignWord() { while (buf.size() % 2) buf.push_back(0); }
    template <typename T> void Push(const T& v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        buf.insert(buf.end(), p, p + sizeof(T));
    }
    void PushStr(const wchar_t* s) {
        AlignWord();
        size_t n = wcslen(s) + 1;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
        buf.insert(buf.end(), p, p + n * sizeof(wchar_t));
    }
};

inline std::vector<uint8_t> BuildDialogTemplate(
    const std::wstring& title, short cx, short cy, bool resizable = true) {
    DlgTemplateBuilder b;
    DLGTEMPLATE t{};
    t.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE |
              DS_SETFONT | DS_CENTER;
    if (resizable) {
        t.style |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    } else {
        t.style |= DS_MODALFRAME;
    }
    t.dwExtendedStyle = WS_EX_CONTROLPARENT;
    t.cdit = 0;
    t.x = 0; t.y = 0;
    t.cx = cx; t.cy = cy;
    b.Push(t);
    b.Push(static_cast<WORD>(0));
    b.Push(static_cast<WORD>(0));
    b.PushStr(title.c_str());
    b.Push(static_cast<WORD>(9));
    b.PushStr(L"Segoe UI");
    return b.buf;
}

inline INT_PTR RunDialogEx(const std::wstring& title, short cx, short cy,
                           HWND owner, DLGPROC proc, void* data,
                           bool resizable = true) {
    std::vector<uint8_t> tmpl = BuildDialogTemplate(title, cx, cy, resizable);
    return DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        reinterpret_cast<LPCDLGTEMPLATE>(tmpl.data()),
        owner, proc, reinterpret_cast<LPARAM>(data));
}

inline int RunDialog(const std::wstring& title, short cx, short cy,
                     HWND owner, DLGPROC proc, void* data) {
    return (RunDialogEx(title, cx, cy, owner, proc, data) < 0) ? 1 : 0;
}

template <typename T>
T* DialogState(HWND hwnd) {
    return reinterpret_cast<T*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

template <typename T>
T* AttachDialogState(HWND hwnd, LPARAM lParam) {
    auto* d = reinterpret_cast<T*>(lParam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
    return d;
}

inline void CenterOnActiveMonitor(HWND hwnd) {
    POINT cursor;
    if (!GetCursorPos(&cursor)) return;
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) return;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    const int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

inline void BringDialogToFront(HWND hwnd) {
    if (!hwnd) return;
    CenterOnActiveMonitor(hwnd);
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);

    const DWORD self  = GetCurrentThreadId();
    const HWND  front = GetForegroundWindow();
    const DWORD owner = front ? GetWindowThreadProcessId(front, nullptr) : 0;
    const bool  share = owner != 0 && owner != self &&
                        AttachThreadInput(self, owner, TRUE) != FALSE;

    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);

    if (share) AttachThreadInput(self, owner, FALSE);

    if (GetForegroundWindow() != hwnd) FlashWindow(hwnd, TRUE);
}

inline bool HandleDialogClose(HWND hwnd, UINT msg, WPARAM wParam) {
    if (msg == WM_CLOSE ||
        (msg == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
        EndDialog(hwnd, 0);
        return true;
    }
    return false;
}

inline void ShowError(HWND owner, const std::wstring& title,
                      const std::wstring& text) {
    MessageBoxW(owner, text.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

struct StackLayout {
    int x = 0;
    int y = 0;
    int w = 0;

    void Place(HWND h, int height) {
        if (!h) return;
        MoveWindow(h, x, y, w, height, TRUE);
        y += height;
    }
    void Gap(int n) { y += n; }
};

struct MenuItem {
    int            id;
    const wchar_t* text;
    bool           enabled = true;
};

inline int TrackMenu(HWND owner, POINT pt,
                     std::initializer_list<MenuItem> items) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return 0;
    for (const MenuItem& item : items) {
        if (!item.text) continue;
        AppendMenuW(menu,
                    MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED),
                    static_cast<UINT_PTR>(item.id), item.text);
    }
    SetForegroundWindow(owner);
    int cmd = static_cast<int>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, owner, nullptr));
    DestroyMenu(menu);
    return cmd;
}

inline BOOL CALLBACK SetChildFontProc(HWND child, LPARAM font) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
    return TRUE;
}

inline void ApplyDialogFont(HWND dlg) {
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
    if (font) {
        EnumChildWindows(dlg, SetChildFontProc,
                         reinterpret_cast<LPARAM>(font));
    }
}

inline std::wstring ControlText(HWND control) {
    int len = GetWindowTextLengthW(control);
    if (len <= 0) return {};
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    int n = GetWindowTextW(control, s.data(), len + 1);
    s.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return s;
}

inline HWND CreateChildControl(HWND parent, const wchar_t* cls,
                               const wchar_t* text, DWORD style, DWORD exStyle,
                               int id, int x, int y, int w, int h) {
    return CreateWindowExW(
        exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

inline HWND CreateLabel(HWND parent, const wchar_t* text) {
    return CreateWindowExW(
        0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

inline HWND CreateStatusBar(HWND parent, int id) {
    return CreateWindowExW(
        0, STATUSCLASSNAMEW, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

inline int StatusBarHeight(HWND status) {
    if (!status) return 0;
    RECT rc{};
    GetWindowRect(status, &rc);
    return rc.bottom - rc.top;
}

inline void SetStatusText(HWND status, const std::wstring& text) {
    if (status) {
        SendMessageW(status, SB_SETTEXTW, 0,
                     reinterpret_cast<LPARAM>(text.c_str()));
    }
}

inline HWND CreateOutputEdit(HWND parent, int id) {
    return CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
            ES_NOHIDESEL,
        0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

inline LRESULT CALLBACK SelectAllEditProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_GETDLGCODE) {
        return DefSubclassProc(hwnd, msg, wParam, lParam) & ~DLGC_HASSETSEL;
    }
    if (msg == WM_SETFOCUS) {
        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        DWORD start = 0, end = 0;
        SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                     reinterpret_cast<LPARAM>(&end));
        if (start == 0 && end != 0 &&
            end == static_cast<DWORD>(GetWindowTextLengthW(hwnd))) {
            SendMessageW(hwnd, EM_SETSEL, 0, 0);
            SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
        }
        return r;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, SelectAllEditProc, 1);
    } else if (msg == WM_KEYDOWN && wParam == 'A' &&
               (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
               (GetKeyState(VK_MENU) & 0x8000) == 0) {
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
        return 0;
    } else if (msg == WM_CHAR && wParam == 1) {
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

inline void EnableSelectAll(HWND edit) {
    if (edit) SetWindowSubclass(edit, SelectAllEditProc, 1, 0);
}

inline void SetOutputText(HWND edit, const std::wstring& text) {
    if (!edit) return;
    SetWindowTextW(edit, text.c_str());
    int len = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, len, len);
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

inline void AppendOutputText(HWND edit, const std::wstring& text) {
    if (!edit || text.empty()) return;
    int len = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
    SendMessageW(edit, EM_SETSEL, len, len);
    SendMessageW(edit, EM_REPLACESEL, FALSE,
                 reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

inline constexpr DWORD kListViewExStyle =
    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP;

inline HWND CreateListView(HWND parent, int id,
                           DWORD extraStyle = 0, DWORD extraExStyle = 0) {
    HWND h = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            LVS_REPORT | LVS_SHOWSELALWAYS | extraStyle,
        0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (h) {
        ListView_SetExtendedListViewStyle(h, kListViewExStyle | extraExStyle);
    }
    return h;
}

struct Column {
    const wchar_t* name;
    int            width;
    int            fmt = LVCFMT_LEFT;
};

template <size_t N>
void InsertColumns(HWND hList, const Column (&cols)[N]) {
    for (size_t i = 0; i < N; ++i) {
        LVCOLUMNW c{};
        c.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.pszText = const_cast<LPWSTR>(cols[i].name);
        c.cx      = cols[i].width;
        c.fmt     = cols[i].fmt;
        ListView_InsertColumn(hList, static_cast<int>(i), &c);
    }
}

inline int AppendRow(HWND hList, int row, const std::wstring& text) {
    LVITEMW it{};
    it.mask     = LVIF_TEXT;
    it.iItem    = row;
    it.iSubItem = 0;
    it.pszText  = const_cast<LPWSTR>(text.c_str());
    return ListView_InsertItem(hList, &it);
}

inline void SetRowText(HWND hList, int row, int col, const std::wstring& s) {
    ListView_SetItemText(hList, row, col, const_cast<LPWSTR>(s.c_str()));
}

inline void AppendRowCells(HWND hList, int row,
                           std::initializer_list<std::wstring> cells) {
    int col = 0;
    int inserted = row;
    for (const std::wstring& cell : cells) {
        if (col == 0) inserted = AppendRow(hList, row, cell);
        else          SetRowText(hList, inserted, col, cell);
        ++col;
    }
}

inline int SelectedRow(HWND hList) {
    return static_cast<int>(SendMessageW(hList, LVM_GETNEXTITEM,
                                         static_cast<WPARAM>(-1),
                                         LVNI_SELECTED));
}

inline void SelectRow(HWND hList, int row) {
    ListView_SetItemState(hList, row,
                          LVIS_FOCUSED | LVIS_SELECTED,
                          LVIS_FOCUSED | LVIS_SELECTED);
}

inline void SelectAllRows(HWND hList) {
    ListView_SetItemState(hList, -1, LVIS_SELECTED, LVIS_SELECTED);
}

inline std::vector<int> SelectedRows(HWND hList) {
    std::vector<int> rows;
    int i = -1;
    for (;;) {
        i = static_cast<int>(SendMessageW(hList, LVM_GETNEXTITEM,
                                          static_cast<WPARAM>(i),
                                          LVNI_SELECTED));
        if (i < 0) break;
        rows.push_back(i);
    }
    return rows;
}

inline bool RowSelected(HWND hList, int row) {
    return (ListView_GetItemState(hList, row, LVIS_SELECTED) &
            LVIS_SELECTED) != 0;
}

inline void SelectOnlyRow(HWND hList, int row) {
    ListView_SetItemState(hList, -1, 0, LVIS_SELECTED);
    SelectRow(hList, row);
}

inline int SelectedIndexIn(HWND hList, size_t count) {
    int i = SelectedRow(hList);
    return (i >= 0 && static_cast<size_t>(i) < count) ? i : -1;
}

inline std::wstring NormalizeCRLF(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + s.size() / 32);
    for (wchar_t c : s) {
        if (c == L'\n') out += L"\r\n";
        else if (c != L'\r') out += c;
    }
    return out;
}

inline void SetReadOnlyText(HWND hEdit, const std::wstring& text) {
    if (!hEdit) return;
    std::wstring normalized = NormalizeCRLF(text);
    SetWindowTextW(hEdit, normalized.c_str());
    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_LINESCROLL, 0, -0x7FFFFFFF);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
}
}

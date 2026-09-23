#pragma once

#include "util/Text.hpp"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace git_tools {

inline constexpr UINT WM_GITTOOLS_TRANSCRIPT = WM_APP + 1;
inline constexpr UINT WM_GITTOOLS_ACTIVATE   = WM_APP + 2;
inline constexpr UINT WM_GITTOOLS_WINDOW     = WM_APP + 16;

struct DlgTemplateBuilder {
    std::vector<uint8_t> buf;
    void AlignWord() { while (buf.size() % 2) buf.push_back(0); }
    template <typename T> void Push(const T& v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        buf.insert(buf.end(), p, p + sizeof(T));
    }
    void PushStr(const wchar_t* s) {
        AlignWord();
        const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
        buf.insert(buf.end(), p, p + (wcslen(s) + 1) * sizeof(wchar_t));
    }
};

inline std::vector<uint8_t> BuildDialogTemplate(
    const std::wstring& title, short cx, short cy, bool resizable = true) {
    DlgTemplateBuilder b;
    DLGTEMPLATE t{};
    t.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE |
              DS_SETFONT | DS_CENTER |
              (resizable ? WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
                         : DS_MODALFRAME);
    t.dwExtendedStyle = WS_EX_CONTROLPARENT;
    t.cx = cx;
    t.cy = cy;
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
T* DialogState(HWND hwnd, UINT msg, LPARAM lParam) {
    if (msg == WM_INITDIALOG) SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);
    return reinterpret_cast<T*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

inline void ShowCouldNotOpen(HWND owner, const std::wstring& title,
                             const std::wstring& path) {
    ShowError(owner, title, L"Could not open:\n\n" + path);
}

inline bool KeyDown(int vkey) {
    return (GetKeyState(vkey) & 0x8000) != 0;
}

inline bool CtrlPressed() {
    return KeyDown(VK_CONTROL) && !KeyDown(VK_MENU);
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
    const int cmd = static_cast<int>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, owner, nullptr));
    DestroyMenu(menu);
    return cmd;
}

inline void ApplyDialogFont(HWND dlg) {
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
    if (!font) return;
    EnumChildWindows(
        dlg,
        [](HWND child, LPARAM f) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(font));
}

inline std::wstring ControlText(HWND control) {
    const int len = GetWindowTextLengthW(control);
    if (len <= 0) return {};
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    s.resize(static_cast<size_t>(std::max(0, GetWindowTextW(control, s.data(),
                                                            len + 1))));
    return s;
}

inline HWND CreateChildControl(HWND parent, const wchar_t* cls,
                               const wchar_t* text, DWORD style, DWORD exStyle,
                               int id, int x = 0, int y = 0, int w = 0,
                               int h = 0) {
    return CreateWindowExW(
        exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

inline HWND CreateLabel(HWND parent, const wchar_t* text) {
    return CreateChildControl(parent, L"STATIC", text, SS_LEFT, 0, 0);
}

inline HWND CreateStatusBar(HWND parent, int id) {
    return CreateChildControl(parent, STATUSCLASSNAMEW, L"", SBARS_SIZEGRIP, 0,
                              id);
}

inline HWND CreateReadOnlyEdit(HWND parent, int id, DWORD extraStyle) {
    return CreateChildControl(
        parent, L"EDIT", L"",
        WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
            ES_NOHIDESEL | extraStyle,
        WS_EX_CLIENTEDGE, id);
}

inline HWND CreateOutputEdit(HWND parent, int id) {
    return CreateReadOnlyEdit(parent, id,
                              WS_TABSTOP | WS_HSCROLL | ES_AUTOHSCROLL);
}

inline constexpr int kButtonWidth  = 82;
inline constexpr int kButtonHeight = 26;

inline void CreateOkCancelButtons(HWND dlg, int margin, int gap) {
    RECT rc;
    GetClientRect(dlg, &rc);
    const int y = rc.bottom - margin - kButtonHeight;
    CreateChildControl(dlg, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
                       IDOK, rc.right - margin - 2 * kButtonWidth - gap, y,
                       kButtonWidth, kButtonHeight);
    CreateChildControl(dlg, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 0,
                       IDCANCEL, rc.right - margin - kButtonWidth, y,
                       kButtonWidth, kButtonHeight);
}

inline void SetChecked(HWND button, bool on) {
    SendMessageW(button, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

inline bool IsChecked(HWND button) {
    return SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

inline std::pair<DWORD, DWORD> EditSelection(HWND edit) {
    DWORD start = 0, end = 0;
    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                 reinterpret_cast<LPARAM>(&end));
    return {start, end};
}

inline int CaretLine(HWND edit) {
    return static_cast<int>(
        SendMessageW(edit, EM_LINEFROMCHAR, EditSelection(edit).first, 0));
}

inline LRESULT CALLBACK SelectAllEditProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_GETDLGCODE) {
        return DefSubclassProc(hwnd, msg, wParam, lParam) & ~DLGC_HASSETSEL;
    }
    if (msg == WM_SETFOCUS) {
        const LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        const auto [start, end] = EditSelection(hwnd);
        if (start == 0 && end != 0 &&
            end == static_cast<DWORD>(GetWindowTextLengthW(hwnd))) {
            SendMessageW(hwnd, EM_SETSEL, 0, 0);
            SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
        }
        return r;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, SelectAllEditProc, 1);
    } else if (msg == WM_KEYDOWN && wParam == 'A' && CtrlPressed()) {
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
    const int len = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, len, len);
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

inline void AppendOutputText(HWND edit, const std::wstring& text) {
    if (!edit || text.empty()) return;
    const int len = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
    SendMessageW(edit, EM_SETSEL, len, len);
    SendMessageW(edit, EM_REPLACESEL, FALSE,
                 reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

inline void SetReadOnlyText(HWND edit, const std::wstring& text) {
    if (!edit) return;
    SetWindowTextW(edit, NormalizeCRLF(text).c_str());
    SendMessageW(edit, EM_SETSEL, 0, 0);
    SendMessageW(edit, EM_LINESCROLL, 0, -0x7FFFFFFF);
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

inline HWND CreateListView(HWND parent, int id,
                           DWORD extraStyle = 0, DWORD extraExStyle = 0) {
    HWND h = CreateChildControl(
        parent, WC_LISTVIEWW, L"",
        WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | extraStyle, 0, id);
    if (h) {
        ListView_SetExtendedListViewStyle(
            h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP |
                   extraExStyle);
    }
    return h;
}

struct Column {
    const wchar_t* name;
    int            width;
    int            fmt = LVCFMT_LEFT;
};

inline void InsertColumns(HWND list, std::span<const Column> cols) {
    for (size_t i = 0; i < cols.size(); ++i) {
        LVCOLUMNW c{};
        c.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.pszText = const_cast<LPWSTR>(cols[i].name);
        c.cx      = cols[i].width;
        c.fmt     = cols[i].fmt;
        ListView_InsertColumn(list, static_cast<int>(i), &c);
    }
}

inline void AppendRowCells(HWND list, int row,
                           std::initializer_list<std::wstring> cells) {
    int col = 0;
    for (const std::wstring& cell : cells) {
        LPWSTR text = const_cast<LPWSTR>(cell.c_str());
        if (col++ == 0) {
            LVITEMW it{};
            it.mask    = LVIF_TEXT;
            it.iItem   = row;
            it.pszText = text;
            row = ListView_InsertItem(list, &it);
        } else {
            ListView_SetItemText(list, row, col - 1, text);
        }
    }
}

inline void SetRowCount(HWND list, size_t count, DWORD flags = 0) {
    ListView_SetItemCountEx(list, static_cast<int>(count), flags);
}

inline int NextSelectedRow(HWND list, int after) {
    return static_cast<int>(SendMessageW(list, LVM_GETNEXTITEM,
                                         static_cast<WPARAM>(after),
                                         LVNI_SELECTED));
}

inline int SelectedRow(HWND list) {
    return NextSelectedRow(list, -1);
}

inline std::vector<int> SelectedRows(HWND list) {
    std::vector<int> rows;
    for (int i = NextSelectedRow(list, -1); i >= 0; i = NextSelectedRow(list, i)) {
        rows.push_back(i);
    }
    return rows;
}

inline void SelectRow(HWND list, int row) {
    ListView_SetItemState(list, row, LVIS_FOCUSED | LVIS_SELECTED,
                          LVIS_FOCUSED | LVIS_SELECTED);
}

inline void SelectAllRows(HWND list) {
    ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
}

inline bool RowSelected(HWND list, int row) {
    return (ListView_GetItemState(list, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
}

inline void SelectOnlyRow(HWND list, int row) {
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
    SelectRow(list, row);
}

inline int SelectedIndexIn(HWND list, size_t count) {
    const int i = SelectedRow(list);
    return (i >= 0 && static_cast<size_t>(i) < count) ? i : -1;
}

}

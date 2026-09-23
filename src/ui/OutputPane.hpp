#pragma once

#include "git/Config.hpp"
#include "git/Transcript.hpp"
#include "ui/DialogUtil.hpp"

#include <string>

namespace git_tools {

inline bool& DebugOutputFlag() {
    static bool on = ConfigGetBool(kDebugOutputKey, false);
    return on;
}

inline bool DebugOutputEnabled() { return DebugOutputFlag(); }

inline void SetDebugOutput(bool on) {
    DebugOutputFlag() = on;
    ConfigSetBool(kDebugOutputKey, on);
}

struct OutputPane {
    HWND               label  = nullptr;
    HWND               edit   = nullptr;
    HWND               status = nullptr;
    unsigned long long cursor = 0;

    void Create(HWND parent, int editId, int statusId) {
        if (DebugOutputEnabled()) CreateEdit(parent, editId);
        status = CreateStatusBar(parent, statusId);
        SetTranscriptTarget(parent, WM_GITTOOLS_TRANSCRIPT);
    }

    bool visible() const { return edit != nullptr; }

    void SetVisible(HWND parent, int editId, bool on) {
        if (on == visible()) return;
        if (on) {
            CreateEdit(parent, editId);
            cursor = 0;
            return;
        }
        DestroyWindow(edit);
        if (label) DestroyWindow(label);
        edit  = nullptr;
        label = nullptr;
    }

    void Refresh(const std::wstring& statusText) {
        if (edit) {
            TranscriptChunk chunk = TranscriptSince(cursor);
            if (chunk.reset) SetOutputText(edit, chunk.text);
            else             AppendOutputText(edit, chunk.text);
        }
        SetStatusText(statusText);
    }

    void Refresh() { Refresh(TranscriptStatus()); }

    void SetStatusText(const std::wstring& text) const {
        if (status) {
            SendMessageW(status, SB_SETTEXTW, 0,
                         reinterpret_cast<LPARAM>(text.c_str()));
        }
    }

    int StatusHeight() const {
        if (!status) return 0;
        SendMessageW(status, WM_SIZE, 0, 0);
        RECT rc{};
        GetWindowRect(status, &rc);
        return rc.bottom - rc.top;
    }

private:
    void CreateEdit(HWND parent, int editId) {
        label = CreateLabel(parent, L"&Output");
        edit  = CreateOutputEdit(parent, editId);
        EnableSelectAll(edit);
    }
};

}

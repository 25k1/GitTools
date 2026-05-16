#pragma once

#include "git/Config.hpp"
#include "git/Transcript.hpp"
#include "ui/DialogUtil.hpp"

#include <string>

namespace git_tools {

inline bool& DebugOutputFlag() {
    static bool on = ConfigGetBool(L"debug", false);
    return on;
}

inline bool DebugOutputEnabled() { return DebugOutputFlag(); }

inline void SetDebugOutput(bool on) {
    DebugOutputFlag() = on;
    ConfigSetBool(L"debug", on);
}

struct OutputPane {
    HWND               label  = nullptr;
    HWND               edit   = nullptr;
    HWND               status = nullptr;
    unsigned long long cursor = 0;

    void Create(HWND parent, int editId, int statusId, UINT notifyMessage) {
        if (DebugOutputEnabled()) {
            label = CreateLabel(parent, L"&Output");
            edit  = CreateOutputEdit(parent, editId);
            EnableSelectAll(edit);
        }
        status = CreateStatusBar(parent, statusId);
        SetTranscriptTarget(parent, notifyMessage);
    }

    bool visible() const { return edit != nullptr; }

    void SetVisible(HWND parent, int editId, bool on) {
        if (on == visible()) return;
        if (on) {
            label  = CreateLabel(parent, L"&Output");
            edit   = CreateOutputEdit(parent, editId);
            EnableSelectAll(edit);
            cursor = 0;
        } else {
            if (edit)  DestroyWindow(edit);
            if (label) DestroyWindow(label);
            edit  = nullptr;
            label = nullptr;
        }
    }

    void Refresh(const std::wstring& statusText) {
        if (edit) {
            TranscriptChunk chunk = TranscriptSince(cursor);
            if (chunk.reset) SetOutputText(edit, chunk.text);
            else             AppendOutputText(edit, chunk.text);
        }
        SetStatusText(status, statusText);
    }

    void Refresh() { Refresh(TranscriptStatus()); }

    int StatusHeight() const {
        if (status) SendMessageW(status, WM_SIZE, 0, 0);
        return StatusBarHeight(status);
    }
};

}

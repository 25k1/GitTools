#include "ui/App.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/display.h>
#include <wx/msgdlg.h>
#include <wx/toplevel.h>
#include <wx/utils.h>

#include <algorithm>
#include <utility>

namespace git_tools {

namespace {

std::function<int()>& StartFunction() {
    static std::function<int()> start;
    return start;
}

#ifdef _WIN32

void ForceForeground(wxTopLevelWindow* window) {
    const HWND  hwnd  = static_cast<HWND>(window->GetHWND());
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

    if (GetForegroundWindow() != hwnd) window->RequestUserAttention();
}

#else

void ForceForeground(wxTopLevelWindow* window) {
    window->Raise();
}

#endif

}

class GitToolsApp : public wxApp {
public:
    bool OnInit() override {
        const int code = StartFunction() ? StartFunction()() : 1;
        if (!wxTopLevelWindows.IsEmpty()) return true;
        SetErrorExitCode(code);
        return false;
    }
};

int RunGui(std::function<int()> start) {
    wxDISABLE_DEBUG_SUPPORT();
    StartFunction() = std::move(start);
    wchar_t name[] = L"gittools";
    wchar_t* argv[] = {name, nullptr};
    int argc = 1;
    return wxEntry(argc, argv);
}

void ShowOnActiveDisplay(wxTopLevelWindow* window) {
    const int index = wxDisplay::GetFromPoint(wxGetMousePosition());
    if (index == wxNOT_FOUND) {
        window->CentreOnScreen();
    } else {
        const wxRect area =
            wxDisplay(static_cast<unsigned>(index)).GetClientArea();
        const wxSize size(std::min(window->GetSize().x, area.width),
                          std::min(window->GetSize().y, area.height));
        window->SetSize(area.x + (area.width - size.x) / 2,
                        area.y + (area.height - size.y) / 2, size.x, size.y);
    }
    window->Show();
    window->CallAfter([window] { ForceForeground(window); });
}

void ShowError(wxWindow* parent, const std::wstring& title,
               const std::wstring& text) {
    wxMessageBox(text, title, wxOK | wxICON_ERROR, parent);
}

void ShowInfo(wxWindow* parent, const std::wstring& title,
              const std::wstring& text) {
    wxMessageBox(text, title, wxOK | wxICON_INFORMATION, parent);
}

bool SetClipboardText(const std::wstring& text) {
    wxClipboardLocker lock;
    return !!lock && wxTheClipboard->SetData(new wxTextDataObject(text));
}

}

wxIMPLEMENT_APP_NO_MAIN(git_tools::GitToolsApp);

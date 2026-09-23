#pragma once

#include <wx/frame.h>

#include <string>

class wxBoxSizer;
class wxPanel;
class wxStaticText;
class wxTextCtrl;

namespace git_tools {

class ToolFrame : public wxFrame {
public:
    ToolFrame(const std::wstring& title, const wxSize& size);
    ~ToolFrame() override;

protected:
    wxPanel* Panel() const { return panel_; }

    void AddLabel(const wchar_t* text);
    void AddPane(wxWindow* pane, int proportion);
    void FinishLayout(wxWindow* absorber, int outputShare);

    void RefreshTranscript();
    void RefreshStatus();

    virtual std::wstring StatusText() const;
    virtual void         OnOptionsChanged() {}

private:
    void ToggleDebugOutput();
    void ApplyOutputVisibility();

    wxPanel*           panel_        = nullptr;
    wxBoxSizer*        sizer_        = nullptr;
    wxStaticText*      outputLabel_  = nullptr;
    wxTextCtrl*        output_       = nullptr;
    wxWindow*          absorber_     = nullptr;
    int                absorberBase_ = 0;
    int                outputShare_  = 0;
    unsigned long long cursor_       = 0;
};

}

#include "ui/Announce.hpp"

#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>

#include <wx/toplevel.h>
#include <wx/window.h>

namespace git_tools {

namespace {

constexpr wchar_t kClassName[] = L"GitToolsAnnouncer";

constexpr int kNotificationKindActionCompleted           = 2;
constexpr int kNotificationProcessingImportantMostRecent = 1;

using RaiseNotificationEvent =
    HRESULT(WINAPI*)(IRawElementProviderSimple*, int, int, BSTR, BSTR);

class AnnouncerProvider final : public IRawElementProviderSimple {
public:
    explicit AnnouncerProvider(HWND hwnd) : hwnd_(hwnd) {}

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) delete this;
        return static_cast<ULONG>(refs);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) {
            *out = static_cast<IRawElementProviderSimple*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_INVALIDARG;
        *out = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_INVALIDARG;
        out->vt = VT_EMPTY;
        if (id == UIA_ControlTypePropertyId) {
            out->vt   = VT_I4;
            out->lVal = UIA_TextControlTypeId;
        } else if (id == UIA_IsControlElementPropertyId ||
                   id == UIA_IsContentElementPropertyId) {
            out->vt      = VT_BOOL;
            out->boolVal = VARIANT_FALSE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE
    get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        return UiaHostProviderFromHwnd(hwnd_, out);
    }

private:
    HWND          hwnd_;
    volatile LONG refs_ = 1;
};

AnnouncerProvider* ProviderOf(HWND hwnd) {
    return reinterpret_cast<AnnouncerProvider*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK AnnouncerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AnnouncerProvider* provider = ProviderOf(hwnd);
    if (message == WM_GETOBJECT && provider &&
        static_cast<long>(lParam) == static_cast<long>(UiaRootObjectId)) {
        return UiaReturnRawElementProvider(hwnd, wParam, lParam, provider);
    }
    if (message == WM_DESTROY && provider) {
        UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        provider->Release();
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND AnnouncerWindow(wxWindow* window) {
    if (!window) return nullptr;
    wxWindow* top    = wxGetTopLevelParent(window);
    const HWND parent = static_cast<HWND>((top ? top : window)->GetHWND());
    if (HWND existing = FindWindowExW(parent, nullptr, kClassName, nullptr)) {
        return existing;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    static const ATOM registered = [instance] {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = AnnouncerProc;
        wc.hInstance     = instance;
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc);
    }();
    if (!registered) return nullptr;

    HWND hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                parent, nullptr, instance, nullptr);
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(new AnnouncerProvider(hwnd)));
    }
    return hwnd;
}

RaiseNotificationEvent RaiseFunction() {
    const HMODULE module = GetModuleHandleW(L"UIAutomationCore.dll");
    return module ? reinterpret_cast<RaiseNotificationEvent>(reinterpret_cast<void (*)()>(
                        GetProcAddress(module, "UiaRaiseNotificationEvent")))
                  : nullptr;
}

}

void PrepareAnnouncements(wxWindow* window) {
    AnnouncerWindow(window);
}

void Announce(wxWindow* window, const std::wstring& text) {
    static const RaiseNotificationEvent raise = RaiseFunction();
    const HWND hwnd = AnnouncerWindow(window);
    AnnouncerProvider* provider = hwnd ? ProviderOf(hwnd) : nullptr;
    if (!raise || !provider) return;

    BSTR message  = SysAllocString(text.c_str());
    BSTR activity = SysAllocString(L"gittools");
    raise(provider, kNotificationKindActionCompleted,
          kNotificationProcessingImportantMostRecent, message, activity);
    SysFreeString(activity);
    SysFreeString(message);
}

}

#include "cli/Dispatch.hpp"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

int wmain(int argc, wchar_t** argv) {
    INITCOMMONCONTROLSEX icc{
        sizeof(icc),
        ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    const HRESULT com =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const int result = git_tools::Dispatch(argc, argv);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}

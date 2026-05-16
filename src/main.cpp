#include "cli/Dispatch.hpp"

#include <windows.h>
#include <commctrl.h>

int wmain(int argc, wchar_t** argv) {
    INITCOMMONCONTROLSEX icc{
        sizeof(icc),
        ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    return git_tools::Dispatch(argc, argv);
}

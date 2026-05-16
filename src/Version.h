#ifndef GITTOOLS_VERSION_H
#define GITTOOLS_VERSION_H

#define GITTOOLS_VERSION_FIELDS 0,4,0,0
#define GITTOOLS_VERSION_STR    "0.4"
#define GITTOOLS_VERSION_FULL   "0.4.0.0"

#ifndef RC_INVOKED

namespace git_tools {

inline constexpr wchar_t kVersion[] = L"" GITTOOLS_VERSION_STR;

}

#endif

#endif

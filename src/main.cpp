#include "cli/Dispatch.hpp"

#ifdef _WIN32

int wmain(int argc, wchar_t** argv) {
    return git_tools::Dispatch(argc, argv);
}

#else

#include "util/Encoding.hpp"

#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::wstring> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) args.push_back(git_tools::Utf8ToWide(argv[i]));
    std::vector<wchar_t*> wargv;
    for (std::wstring& a : args) wargv.push_back(a.data());
    wargv.push_back(nullptr);
    return git_tools::Dispatch(argc, wargv.data());
}

#endif

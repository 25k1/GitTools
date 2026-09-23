#include "cli/Dispatch.hpp"

int wmain(int argc, wchar_t** argv) {
    return git_tools::Dispatch(argc, argv);
}

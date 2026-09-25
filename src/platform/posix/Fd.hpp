#pragma once

#include "util/Encoding.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>

#include <string>
#include <string_view>
#include <vector>

namespace git_tools {

inline bool WriteAll(int fd, std::string_view data) {
    for (size_t offset = 0; offset < data.size();) {
        const ssize_t n = write(fd, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        offset += static_cast<size_t>(n);
    }
    return true;
}

template <typename F>
void ReadAll(int fd, F&& consume) {
    char buf[65536];
    for (;;) {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        consume(std::string_view(buf, static_cast<size_t>(n)));
    }
}

inline std::wstring ErrorText(int code) {
    return Utf8ToWide(std::strerror(code));
}

class ArgvList {
public:
    ArgvList(const std::wstring& executable, const std::vector<std::wstring>& args) {
        store_.reserve(args.size() + 1);
        store_.push_back(WideToUtf8(executable));
        for (const std::wstring& a : args) store_.push_back(WideToUtf8(a));
        for (std::string& a : store_) argv_.push_back(a.data());
        argv_.push_back(nullptr);
    }

    const char*  file() const { return argv_.front(); }
    char* const* get() { return argv_.data(); }

private:
    std::vector<std::string> store_;
    std::vector<char*>       argv_;
};

[[noreturn]] inline void ReportExecFailure(int fd) {
    const int err = errno;
    (void)!write(fd, &err, sizeof(err));
    _exit(127);
}

inline bool ReadExecFailure(int fd, int& error) {
    ssize_t got = 0;
    do {
        got = read(fd, &error, sizeof(error));
    } while (got < 0 && errno == EINTR);
    return got == static_cast<ssize_t>(sizeof(error));
}

}

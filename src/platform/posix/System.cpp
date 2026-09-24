#include "util/System.hpp"

#include "util/Encoding.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

namespace git_tools {

namespace {

thread_local int lastError = 0;

void WriteLine(int fd, const std::wstring& s) {
    const std::string bytes = WideToUtf8(s) + "\n";
    size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t n = write(fd, bytes.data() + offset, bytes.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        offset += static_cast<size_t>(n);
    }
}

void RedirectToNull() {
    const int null = open("/dev/null", O_RDWR);
    if (null < 0) return;
    dup2(null, 0);
    dup2(null, 1);
    dup2(null, 2);
    if (null > 2) close(null);
}

}

std::wstring ExecutablePath() {
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf));
    return (n <= 0 || n >= static_cast<ssize_t>(sizeof(buf)))
               ? std::wstring()
               : Utf8ToWide(std::string_view(buf, static_cast<size_t>(n)));
}

std::wstring CurrentDirectory() {
    std::string buf(256, '\0');
    while (!getcwd(buf.data(), buf.size())) {
        if (errno != ERANGE) return {};
        buf.resize(buf.size() * 2);
    }
    return Utf8ToWide(buf.c_str());
}

bool SpawnDetachedProcess(const std::wstring& cwd,
                          const std::wstring& executable,
                          const std::vector<std::wstring>& args) {
    std::vector<std::string> argStore;
    argStore.push_back(WideToUtf8(executable));
    for (const std::wstring& a : args) argStore.push_back(WideToUtf8(a));
    std::vector<char*> argv;
    for (std::string& a : argStore) argv.push_back(a.data());
    argv.push_back(nullptr);
    const std::string dir = WideToUtf8(cwd);

    int status[2];
    if (pipe2(status, O_CLOEXEC) != 0) {
        lastError = errno;
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        lastError = errno;
        close(status[0]);
        close(status[1]);
        return false;
    }
    if (pid == 0) {
        setsid();
        const pid_t child = fork();
        if (child > 0) _exit(0);
        if (child == 0) {
            RedirectToNull();
            if (dir.empty() || chdir(dir.c_str()) == 0) {
                execvp(argv[0], argv.data());
            }
        }
        const int err = errno;
        (void)!write(status[1], &err, sizeof(err));
        _exit(127);
    }

    close(status[1]);
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    int err = 0;
    ssize_t got = 0;
    do {
        got = read(status[0], &err, sizeof(err));
    } while (got < 0 && errno == EINTR);
    close(status[0]);
    if (got == static_cast<ssize_t>(sizeof(err))) {
        lastError = err;
        return false;
    }
    return true;
}

std::wstring LastSystemError() {
    return Utf8ToWide(std::strerror(lastError));
}

void UseUtf8Console() {}

void ReleaseConsole() {}

void WriteOut(const std::wstring& s) {
    WriteLine(1, s);
}

void WriteErr(const std::wstring& s) {
    WriteLine(2, s);
}

}

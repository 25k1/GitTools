#include "util/System.hpp"

#include "platform/posix/Fd.hpp"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>

namespace git_tools {

namespace {

thread_local int lastError = 0;

void WriteLine(int fd, const std::wstring& s) {
    WriteAll(fd, WideToUtf8(s) + "\n");
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
    ArgvList          argv(executable, args);
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
                execvp(argv.file(), argv.get());
            }
        }
        ReportExecFailure(status[1]);
    }

    close(status[1]);
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    int        err    = 0;
    const bool failed = ReadExecFailure(status[0], err);
    close(status[0]);
    if (failed) lastError = err;
    return !failed;
}

std::wstring LastSystemError() {
    return ErrorText(lastError);
}

bool ReadStandardInput(std::string& bytes) {
    if (isatty(0)) return false;
    ReadAll(0, [&](std::string_view chunk) { bytes.append(chunk); });
    return true;
}

std::string ReadFileBytes(const std::wstring& path) {
    constexpr long kMaxBytes = 32L * 1024 * 1024;

    std::FILE* file = std::fopen(WideToUtf8(path).c_str(), "rb");
    if (!file) return {};

    std::string out;
    if (std::fseek(file, 0, SEEK_END) == 0) {
        const long size = std::ftell(file);
        if (size > 0 && size <= kMaxBytes && std::fseek(file, 0, SEEK_SET) == 0) {
            out.resize(static_cast<size_t>(size));
            out.resize(std::fread(out.data(), 1, out.size(), file));
        }
    }
    std::fclose(file);

    if (out.starts_with("\xEF\xBB\xBF")) out.erase(0, 3);
    return out;
}

std::wstring WriteTempFile(std::string_view bytes) {
    const char* dir  = std::getenv("TMPDIR");
    std::string path = std::string(dir && *dir ? dir : "/tmp") + "/gittools-diff-XXXXXX";
    const int   fd   = mkstemp(path.data());
    if (fd < 0) {
        lastError = errno;
        return {};
    }
    const bool ok = WriteAll(fd, bytes);
    if (!ok) lastError = errno ? errno : EIO;
    close(fd);
    if (ok) return Utf8ToWide(path);
    unlink(path.c_str());
    return {};
}

void RemoveFile(const std::wstring& path) {
    unlink(WideToUtf8(path).c_str());
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

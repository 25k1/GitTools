#include "git/Process.hpp"

#include "util/Encoding.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <thread>
#include <vector>

namespace git_tools {

namespace {

struct Fd {
    int fd = -1;

    ~Fd() { Close(); }

    void Close() {
        if (fd >= 0) close(fd);
        fd = -1;
    }
};

struct Pipe {
    Fd readEnd;
    Fd writeEnd;

    bool Open() {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) != 0) return false;
        readEnd.fd  = fds[0];
        writeEnd.fd = fds[1];
        return true;
    }
};

void IgnoreBrokenPipes() {
    static const bool ignored = [] {
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)ignored;
}

void WriteAll(int fd, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = write(fd, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        offset += static_cast<size_t>(n);
    }
}

void DrainPipe(int fd, std::string& out, const OutputSink& sink = {}) {
    char buf[16384];
    for (;;) {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (sink) sink(std::string_view(buf, static_cast<size_t>(n)));
        else      out.append(buf, static_cast<size_t>(n));
    }
}

std::wstring ErrorText(int code) {
    return Utf8ToWide(std::strerror(code));
}

[[noreturn]] void ReportExecFailure(int fd) {
    const int err = errno;
    (void)!write(fd, &err, sizeof(err));
    _exit(127);
}

int ExitCodeOf(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

}

void KillProcess(ProcessId process) {
    kill(static_cast<pid_t>(process), SIGKILL);
}

ProcessResult RunProcess(const std::wstring& executable,
                         const std::vector<std::wstring>& args,
                         const std::wstring& cwd,
                         StdioMode stdio,
                         ProcessCanceller* cancel,
                         const OutputSink& onStdout,
                         const std::string* input) {
    ProcessResult result;
    IgnoreBrokenPipes();

    std::vector<std::string> argStore;
    argStore.reserve(args.size() + 1);
    argStore.push_back(WideToUtf8(executable));
    for (const std::wstring& a : args) argStore.push_back(WideToUtf8(a));
    std::vector<char*> argv;
    for (std::string& a : argStore) argv.push_back(a.data());
    argv.push_back(nullptr);
    const std::string dir = WideToUtf8(cwd);

    const bool capture = stdio == StdioMode::Capture;
    Pipe inPipe, outPipe, errPipe, execPipe;
    if ((capture && (!outPipe.Open() || !errPipe.Open() ||
                     (input && !inPipe.Open()))) ||
        !execPipe.Open()) {
        result.errorMessage = L"failed to create pipes: " + ErrorText(errno);
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        result.errorMessage = L"fork failed: " + ErrorText(errno);
        return result;
    }
    if (pid == 0) {
        std::signal(SIGPIPE, SIG_DFL);
        if (capture) {
            const int in = input ? inPipe.readEnd.fd : open("/dev/null", O_RDONLY);
            dup2(in, 0);
            dup2(outPipe.writeEnd.fd, 1);
            dup2(errPipe.writeEnd.fd, 2);
        }
        if (!dir.empty() && chdir(dir.c_str()) != 0) {
            ReportExecFailure(execPipe.writeEnd.fd);
        }
        execvp(argv[0], argv.data());
        ReportExecFailure(execPipe.writeEnd.fd);
    }

    execPipe.writeEnd.Close();
    int execError = 0;
    ssize_t got = 0;
    do {
        got = read(execPipe.readEnd.fd, &execError, sizeof(execError));
    } while (got < 0 && errno == EINTR);
    if (got == static_cast<ssize_t>(sizeof(execError))) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        result.errorMessage =
            L"failed to start " + executable + L": " + ErrorText(execError);
        return result;
    }
    result.started = true;

    if (cancel && !cancel->Attach(pid)) KillProcess(pid);

    if (capture) {
        outPipe.writeEnd.Close();
        errPipe.writeEnd.Close();
        inPipe.readEnd.Close();

        std::thread tIn;
        if (input) {
            tIn = std::thread([&] {
                WriteAll(inPipe.writeEnd.fd, *input);
                inPipe.writeEnd.Close();
            });
        }
        std::thread tErr([&] { DrainPipe(errPipe.readEnd.fd, result.stderrText); });
        DrainPipe(outPipe.readEnd.fd, result.stdoutText, onStdout);
        tErr.join();
        if (tIn.joinable()) tIn.join();
    }

    siginfo_t info{};
    while (waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOWAIT) < 0 &&
           errno == EINTR) {}
    if (cancel) cancel->Detach();

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    result.exitCode = ExitCodeOf(status);
    return result;
}

}

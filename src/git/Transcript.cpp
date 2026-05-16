#include "git/Transcript.hpp"

#include "ui/Encoding.hpp"

#include <mutex>

namespace git_tools {

namespace {

constexpr size_t kMaxChars      = 200000;
constexpr size_t kMaxEntryChars = 8000;

std::mutex& Mu() {
    static std::mutex m;
    return m;
}

std::wstring& Text() {
    static std::wstring t;
    return t;
}

std::wstring& Status() {
    static std::wstring s;
    return s;
}

unsigned long long gTotal   = 0;
HWND               gTarget  = nullptr;
UINT               gMessage = 0;

void AppendNormalized(std::wstring& dst, const std::wstring& src) {
    if (src.size() > kMaxEntryChars) {
        for (size_t i = 0; i < kMaxEntryChars; ++i) {
            wchar_t c = src[i];
            if (c == L'\n') dst += L"\r\n";
            else if (c != L'\r') dst += c;
        }
        dst += L"\r\n... (output truncated)\r\n";
        return;
    }
    for (wchar_t c : src) {
        if (c == L'\n') dst += L"\r\n";
        else if (c != L'\r') dst += c;
    }
}

std::wstring Command(const std::vector<std::wstring>& args, bool nameOnly) {
    std::wstring out = L"git";
    for (const auto& a : args) {
        out += L' ';
        out += a;
        if (nameOnly) break;
    }
    return out;
}

std::wstring CommandName(const std::vector<std::wstring>& args) {
    return Command(args, true);
}

std::wstring FullCommand(const std::vector<std::wstring>& args) {
    return Command(args, false);
}

void Publish(const std::wstring& entry, const std::wstring& status) {
    HWND target  = nullptr;
    UINT message = 0;
    {
        std::lock_guard<std::mutex> lock(Mu());
        Text()  += entry;
        gTotal  += entry.size();
        if (Text().size() > kMaxChars) {
            size_t cut = Text().size() - kMaxChars;
            size_t nl  = Text().find(L'\n', cut);
            Text().erase(0, nl == std::wstring::npos ? cut : nl + 1);
        }
        Status() = status;
        target   = gTarget;
        message  = gMessage;
    }
    if (target && message) PostMessageW(target, message, 0, 0);
}
}

void SetTranscriptTarget(HWND hwnd, UINT message) {
    std::lock_guard<std::mutex> lock(Mu());
    gTarget  = hwnd;
    gMessage = message;
}

void NoteGitStart(const std::vector<std::wstring>& args) {
    Publish(L"> " + FullCommand(args) + L"\r\n",
            CommandName(args) + L" - running");
}

void RecordGitRun(const std::vector<std::wstring>& args,
                  const ProcessResult& result) {
    std::wstring entry;
    std::wstring status = CommandName(args);

    if (!result.started) {
        AppendNormalized(entry, result.errorMessage);
        status += L" - failed";
    } else {
        AppendNormalized(entry, Utf8ToWide(result.stdoutText));
        AppendNormalized(entry, Utf8ToWide(result.stderrText));
        status += (result.exitCode == 0) ? L" - success" : L" - failed";
    }
    if (entry.size() < 2 || entry.compare(entry.size() - 2, 2, L"\r\n") != 0) {
        entry += L"\r\n";
    }
    Publish(entry, status);
}

void RecordGitCancelled(const std::vector<std::wstring>& args) {
    Publish(L"(cancelled)\r\n", CommandName(args) + L" - cancelled");
}

TranscriptChunk TranscriptSince(unsigned long long& cursor) {
    std::lock_guard<std::mutex> lock(Mu());
    TranscriptChunk chunk;
    const unsigned long long base = gTotal - Text().size();
    if (cursor < base) {
        chunk.reset = true;
        chunk.text  = Text();
    } else {
        chunk.text = Text().substr(static_cast<size_t>(cursor - base));
    }
    cursor = gTotal;
    return chunk;
}

std::wstring TranscriptStatus() {
    std::lock_guard<std::mutex> lock(Mu());
    return Status();
}
}

#include "git/Transcript.hpp"

#include "util/Encoding.hpp"
#include "util/Text.hpp"

#include <mutex>

namespace git_tools {

namespace {

constexpr size_t kMaxChars      = 200000;
constexpr size_t kMaxEntryChars = 8000;

struct TranscriptState {
    std::mutex            mu;
    std::wstring          text;
    std::wstring          status;
    unsigned long long    total = 0;
    std::function<void()> listener;
};

TranscriptState& State() {
    static TranscriptState state;
    return state;
}

void AppendNormalized(std::wstring& dst, std::wstring_view src) {
    dst += NormalizeCRLF(src.substr(0, kMaxEntryChars));
    if (src.size() > kMaxEntryChars) dst += L"\r\n... (output truncated)\r\n";
}

std::wstring CommandName(const std::vector<std::wstring>& args) {
    return args.empty() ? std::wstring(L"git") : L"git " + args.front();
}

void Publish(const std::wstring& entry, const std::wstring& status) {
    TranscriptState& s = State();
    std::lock_guard lock(s.mu);
    s.text  += entry;
    s.total += entry.size();
    if (s.text.size() > kMaxChars) {
        const size_t cut = s.text.size() - kMaxChars;
        const size_t nl  = s.text.find(L'\n', cut);
        s.text.erase(0, nl == std::wstring::npos ? cut : nl + 1);
    }
    s.status = status;
    if (s.listener) s.listener();
}

}

void SetTranscriptListener(std::function<void()> listener) {
    TranscriptState& s = State();
    std::lock_guard lock(s.mu);
    s.listener = std::move(listener);
}

void NoteGitStart(const std::vector<std::wstring>& args) {
    Publish(L"> git " + Join(args, L" ") + L"\r\n",
            CommandName(args) + L" - running");
}

void RecordGitRun(const std::vector<std::wstring>& args,
                  const ProcessResult& result) {
    std::wstring entry;
    if (result.started) {
        AppendNormalized(entry, Utf8ToWide(result.stdoutText));
        AppendNormalized(entry, Utf8ToWide(result.stderrText));
    } else {
        AppendNormalized(entry, result.errorMessage);
    }
    if (!entry.ends_with(L"\r\n")) entry += L"\r\n";
    Publish(entry, CommandName(args) +
                       (result.ok() ? L" - success" : L" - failed"));
}

void RecordGitCancelled(const std::vector<std::wstring>& args) {
    Publish(L"(cancelled)\r\n", CommandName(args) + L" - cancelled");
}

TranscriptChunk TranscriptSince(unsigned long long& cursor) {
    TranscriptState& s = State();
    std::lock_guard lock(s.mu);
    TranscriptChunk chunk;
    const unsigned long long base = s.total - s.text.size();
    if (cursor < base) {
        chunk.reset = true;
        chunk.text  = s.text;
    } else {
        chunk.text = s.text.substr(static_cast<size_t>(cursor - base));
    }
    cursor = s.total;
    return chunk;
}

std::wstring TranscriptStatus() {
    TranscriptState& s = State();
    std::lock_guard lock(s.mu);
    return s.status;
}

}

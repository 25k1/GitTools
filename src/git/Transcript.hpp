#pragma once

#include "git/Process.hpp"

#include <string>
#include <vector>

namespace git_tools {

struct TranscriptChunk {
    std::wstring text;
    bool         reset = false;
};

void SetTranscriptTarget(HWND hwnd, UINT message);

void NoteGitStart(const std::vector<std::wstring>& args);

void RecordGitRun(const std::vector<std::wstring>& args,
                  const ProcessResult& result);

TranscriptChunk TranscriptSince(unsigned long long& cursor);

void RecordGitCancelled(const std::vector<std::wstring>& args);

std::wstring TranscriptStatus();

}

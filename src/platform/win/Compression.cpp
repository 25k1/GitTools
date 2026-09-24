#include "git/Compression.hpp"

#include <windows.h>
#include <compressapi.h>

namespace git_tools {

namespace {

constexpr DWORD kAlgorithm = COMPRESS_ALGORITHM_XPRESS_HUFF;

}

TextCodec::~TextCodec() {
    if (compressor_) {
        CloseCompressor(static_cast<COMPRESSOR_HANDLE>(compressor_));
    }
    if (decompressor_) {
        CloseDecompressor(static_cast<DECOMPRESSOR_HANDLE>(decompressor_));
    }
}

bool TextCodec::Pack(std::string_view raw, std::string& packed) {
    packed.clear();
    if (raw.empty()) return false;
    if (!compressor_) {
        COMPRESSOR_HANDLE handle = nullptr;
        if (!CreateCompressor(kAlgorithm, nullptr, &handle)) return false;
        compressor_ = handle;
    }

    packed.resize(raw.size());
    SIZE_T written = 0;
    if (!Compress(static_cast<COMPRESSOR_HANDLE>(compressor_),
                  raw.data(), raw.size(),
                  packed.data(), packed.size(), &written) ||
        written == 0 || written >= raw.size()) {
        std::string().swap(packed);
        return false;
    }
    packed.resize(written);
    packed.shrink_to_fit();
    return true;
}

bool TextCodec::Unpack(std::string_view packed, size_t rawSize,
                       std::string& raw) {
    raw.clear();
    if (!decompressor_) {
        DECOMPRESSOR_HANDLE handle = nullptr;
        if (!CreateDecompressor(kAlgorithm, nullptr, &handle)) return false;
        decompressor_ = handle;
    }

    raw.resize(rawSize);
    SIZE_T written = 0;
    if (!Decompress(static_cast<DECOMPRESSOR_HANDLE>(decompressor_),
                    packed.data(), packed.size(),
                    raw.data(), raw.size(), &written) ||
        written != rawSize) {
        raw.clear();
        return false;
    }
    return true;
}

}

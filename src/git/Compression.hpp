#pragma once

#include <string>
#include <string_view>

namespace git_tools {

class TextCodec {
public:
    TextCodec() = default;
    ~TextCodec();

    TextCodec(const TextCodec&)            = delete;
    TextCodec& operator=(const TextCodec&) = delete;

    bool Pack(std::string_view raw, std::string& packed);
    bool Unpack(std::string_view packed, size_t rawSize, std::string& raw);

private:
    void* compressor_   = nullptr;
    void* decompressor_ = nullptr;
};

}

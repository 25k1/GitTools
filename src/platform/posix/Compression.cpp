#include "git/Compression.hpp"

#include <zlib.h>

namespace git_tools {

TextCodec::~TextCodec() = default;

bool TextCodec::Pack(std::string_view raw, std::string& packed) {
    packed.clear();
    if (raw.empty()) return false;

    uLongf written = compressBound(static_cast<uLong>(raw.size()));
    packed.resize(written);
    if (compress2(reinterpret_cast<Bytef*>(packed.data()), &written,
                  reinterpret_cast<const Bytef*>(raw.data()),
                  static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK ||
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
    raw.resize(rawSize);
    uLongf written = static_cast<uLongf>(rawSize);
    if (uncompress(reinterpret_cast<Bytef*>(raw.data()), &written,
                   reinterpret_cast<const Bytef*>(packed.data()),
                   static_cast<uLong>(packed.size())) != Z_OK ||
        written != rawSize) {
        raw.clear();
        return false;
    }
    return true;
}

}

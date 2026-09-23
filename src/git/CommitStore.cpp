#include "git/CommitStore.hpp"

#include "ui/Encoding.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace git_tools {

namespace {

constexpr size_t   kRowPage         = 4096;
constexpr size_t   kMaxDateLen      = 255;
constexpr size_t   kMaxSubjectLen   = 512 * 1024;
constexpr uint32_t kTextDate        = 0xFFFFFFFF;
constexpr char     kPersonSeparator = '\x1f';

template <typename CharT>
int HexValue(CharT c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

template <typename CharT>
size_t DecodeHex(std::basic_string_view<CharT> hex, uint8_t (&out)[32]) {
    if (hex.empty() || hex.size() % 2 != 0 || hex.size() > 2 * sizeof(out)) {
        return 0;
    }
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = HexValue(hex[i]);
        const int lo = HexValue(hex[i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return hex.size() / 2;
}

template <typename S>
S EncodeHex(const uint8_t* bytes, size_t digits) {
    static constexpr char kDigits[] = "0123456789abcdef";
    S out(digits, '0');
    for (size_t i = 0; i < digits; ++i) {
        const uint8_t b = bytes[i / 2];
        out[i] = static_cast<typename S::value_type>(
            kDigits[(i % 2 == 0) ? (b >> 4) : (b & 0xF)]);
    }
    return out;
}

bool SplitFields(std::string_view s, char delim, std::string_view* out,
                 size_t count) {
    size_t start = 0;
    for (size_t n = 0; n + 1 < count; ++n) {
        const size_t end = s.find(delim, start);
        if (end == std::string_view::npos) return false;
        out[n] = s.substr(start, end - start);
        start  = end + 1;
    }
    out[count - 1] = s.substr(start);
    return true;
}

int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= (m <= 2) ? 1 : 0;
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void CivilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t  era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp  = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = (mp < 10) ? mp + 3 : mp - 9;
    y = static_cast<int64_t>(yoe) + era * 400 + ((m <= 2) ? 1 : 0);
}

template <typename S>
void AppendPadded(S& out, int64_t value, int width) {
    typename S::value_type digits[20];
    int n = 0;
    do {
        digits[n++] = static_cast<typename S::value_type>('0' + value % 10);
        value /= 10;
    } while (value > 0 && n < 20);
    for (int i = n; i < width; ++i) out += static_cast<typename S::value_type>('0');
    while (n > 0) out += digits[--n];
}

template <typename S>
S FormatDate(uint32_t packed) {
    int64_t  y = 0;
    unsigned m = 0, d = 0;
    CivilFromDays(packed / 86400, y, m, d);
    const uint32_t secs = packed % 86400;
    using C = typename S::value_type;
    S out;
    out.reserve(19);
    AppendPadded(out, y, 4);
    out += static_cast<C>('-');
    AppendPadded(out, m, 2);
    out += static_cast<C>('-');
    AppendPadded(out, d, 2);
    out += static_cast<C>(' ');
    AppendPadded(out, secs / 3600, 2);
    out += static_cast<C>(':');
    AppendPadded(out, secs / 60 % 60, 2);
    out += static_cast<C>(':');
    AppendPadded(out, secs % 60, 2);
    return out;
}

bool ParseNumber(std::string_view s, size_t pos, size_t len, unsigned& out) {
    out = 0;
    for (size_t i = pos; i < pos + len; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        out = out * 10 + static_cast<unsigned>(s[i] - '0');
    }
    return true;
}

bool PackDate(std::string_view s, uint32_t& packed) {
    if (s.size() != 19) return false;
    unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (!ParseNumber(s, 0, 4, y)   || !ParseNumber(s, 5, 2, mo) ||
        !ParseNumber(s, 8, 2, d)   || !ParseNumber(s, 11, 2, h) ||
        !ParseNumber(s, 14, 2, mi) || !ParseNumber(s, 17, 2, se)) {
        return false;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59) {
        return false;
    }
    const int64_t total = DaysFromCivil(y, mo, d) * 86400 +
                          h * 3600 + mi * 60 + se;
    if (total < 0 || total >= kTextDate) return false;
    packed = static_cast<uint32_t>(total);
    return FormatDate<std::string>(packed) == s;
}

}

bool CommitStore::Parse(std::string_view record, Parsed& out) {
    const size_t start = record.find_first_not_of("\r\n");
    if (start == std::string_view::npos) return false;

    std::string_view f[6];
    if (!SplitFields(record.substr(start), '\x1f', f, 6)) return false;

    out.hashLen = DecodeHex(f[0], out.hash);
    if (out.hashLen == 0) return false;
    out.abbrev  = f[1];
    out.name    = f[2];
    out.email   = f[3];
    out.date    = f[4].substr(0, kMaxDateLen);
    out.subject = f[5].substr(0, kMaxSubjectLen);
    return true;
}

bool CommitStore::Append(std::string_view record) {
    Parsed p;
    if (!Parse(record, p)) return false;
    if (hashLen_ == 0) hashLen_ = p.hashLen;
    if (p.hashLen != hashLen_) return false;

    if (pages_.empty() || PageCount(pages_.back()) == kRowPage) {
        if (!pages_.empty()) Seal(pages_.size() - 1);
        Page& fresh = pages_.emplace_back();
        fresh.rows.reserve(kRowPage);
        fresh.hashes.reserve(kRowPage * hashLen_);
    }
    Page& page = pages_.back();
    AddRow(page, p);
    page.hashes.insert(page.hashes.end(), p.hash, p.hash + p.hashLen);
    ++count_;
    return true;
}

Commit CommitStore::At(size_t i) const {
    const Located at   = Locate(i);
    const Page&   page = pages_[at.page];
    Touch(at.page);

    Commit c;
    c.fullSha = Hash(at);
    if (at.slot >= page.rows.size()) {
        c.shortSha = c.fullSha.substr(0, 7);
        return c;
    }

    const Row&             r      = page.rows[at.slot];
    const std::string_view text   = Text(at);
    const std::string_view person = *people_[r.person];
    const size_t           sep    = person.find(kPersonSeparator);
    const size_t           skip   =
        (r.date == kTextDate) ? std::min<size_t>(r.dateLen, text.size()) : 0;

    c.shortSha    = c.fullSha.substr(0, r.abbrevLen);
    c.author      = Utf8ToWide(person.substr(0, sep));
    c.authorEmail = Utf8ToWide(person.substr(sep + 1));
    c.date        = Date(r, text);
    c.subject     = Utf8ToWide(text.substr(skip));
    return c;
}

std::wstring CommitStore::Sha(size_t i) const {
    return Hash(Locate(i));
}

bool CommitStore::Subject(size_t i, std::wstring& out) const {
    const Located at   = Locate(i);
    const Page&   page = pages_[at.page];
    if (at.slot >= page.rows.size()) return false;
    const Row&             r    = page.rows[at.slot];
    const std::string_view text = Text(at);
    const size_t           skip =
        (r.date == kTextDate) ? std::min<size_t>(r.dateLen, text.size()) : 0;
    out = Utf8ToWide(text.substr(skip));
    return true;
}

size_t CommitStore::IndexOf(std::wstring_view sha) const {
    uint8_t target[32];
    const size_t len = DecodeHex(sha, target);
    if (len == 0 || len != hashLen_) return npos;
    size_t index = 0;
    for (const Page& page : pages_) {
        const uint8_t* h     = page.hashes.data();
        const size_t   count = PageCount(page);
        for (size_t slot = 0; slot < count; ++slot, ++index) {
            if (std::memcmp(h + slot * len, target, len) == 0) return index;
        }
    }
    return npos;
}

void CommitStore::SetResidentLimit(size_t pages) {
    residentLimit_ = pages;
    EnforceLimit();
}

bool CommitStore::NeedsRestore(size_t i) const {
    const Page& page = pages_[Locate(i).page];
    return page.unloaded && !page.restoreFailed;
}

std::string CommitStore::Revisions(size_t i) const {
    const Page&  page  = pages_[Locate(i).page];
    const size_t count = PageCount(page);
    std::string out;
    out.reserve(count * (hashLen_ * 2 + 1));
    for (size_t slot = 0; slot < count; ++slot) {
        out += EncodeHex<std::string>(page.hashes.data() + slot * hashLen_,
                                      hashLen_ * 2);
        out += '\n';
    }
    return out;
}

bool CommitStore::Restore(size_t i, std::string_view output) {
    const size_t index = Locate(i).page;
    Page& page = pages_[index];
    if (!page.unloaded) return true;

    const size_t expected = PageCount(page);
    Page fresh;
    fresh.rows.reserve(expected);
    bool ok = true;
    size_t start = 0;
    while (ok && start <= output.size()) {
        const size_t nul = output.find('\0', start);
        const size_t end = (nul == std::string_view::npos) ? output.size() : nul;
        const std::string_view record = output.substr(start, end - start);
        start = end + 1;

        Parsed p;
        if (!Parse(record, p)) continue;
        const size_t slot = fresh.rows.size();
        ok = slot < expected && p.hashLen == hashLen_ &&
             std::memcmp(p.hash, page.hashes.data() + slot * hashLen_,
                         hashLen_) == 0;
        if (ok) AddRow(fresh, p);
    }
    if (!ok || fresh.rows.size() != expected) {
        page.restoreFailed = true;
        return false;
    }

    page.rows     = std::move(fresh.rows);
    page.text     = std::move(fresh.text);
    page.unloaded = false;
    Seal(index);
    return true;
}

void CommitStore::AddRow(Page& page, const Parsed& p) {
    Row r{};
    r.text      = static_cast<uint32_t>(page.text.size());
    r.person    = Intern(p.name, p.email);
    r.abbrevLen = static_cast<uint8_t>(
        std::min(p.abbrev.size(), p.hashLen * 2));
    if (!PackDate(p.date, r.date)) {
        r.date    = kTextDate;
        r.dateLen = static_cast<uint8_t>(p.date.size());
        page.text.append(p.date);
    }
    page.text.append(p.subject);
    page.rows.push_back(r);
}

void CommitStore::Seal(size_t index) {
    Page& page = pages_[index];
    page.sealed = true;
    if (codec_.Pack(page.text, page.packed)) {
        page.rawSize = static_cast<uint32_t>(page.text.size());
        std::string().swap(page.text);
    } else {
        page.text.shrink_to_fit();
    }
    Forget(index);
    Touch(index);
    EnforceLimit();
}

void CommitStore::Unload(size_t index) {
    Page& page = pages_[index];
    std::vector<Row>().swap(page.rows);
    std::string().swap(page.text);
    std::string().swap(page.packed);
    page.rawSize       = 0;
    page.unloaded      = true;
    page.restoreFailed = false;
    Forget(index);
}

void CommitStore::EnforceLimit() {
    if (residentLimit_ == 0) return;
    for (;;) {
        size_t resident = 0;
        size_t oldest   = npos;
        for (size_t i = 0; i < pages_.size(); ++i) {
            const Page& page = pages_[i];
            if (!page.sealed || page.unloaded) continue;
            ++resident;
            if (oldest == npos || page.lastUse < pages_[oldest].lastUse) {
                oldest = i;
            }
        }
        if (resident <= residentLimit_) return;
        Unload(oldest);
    }
}

void CommitStore::Forget(size_t index) const {
    for (CachedPage& cached : cache_) {
        if (cached.page == index) {
            cached.page = npos;
            std::string().swap(cached.text);
        }
    }
}

void CommitStore::Touch(size_t index) const {
    pages_[index].lastUse = ++tick_;
}

size_t CommitStore::PageCount(const Page& page) const {
    return hashLen_ ? page.hashes.size() / hashLen_ : 0;
}

CommitStore::Located CommitStore::Locate(size_t i) const {
    return {i / kRowPage, i % kRowPage};
}

std::string_view CommitStore::PageText(size_t index) const {
    const Page& page = pages_[index];
    if (page.packed.empty()) return page.text;
    for (const CachedPage& cached : cache_) {
        if (cached.page == index) return cached.text;
    }
    CachedPage& slot = cache_[cacheNext_];
    cacheNext_ = (cacheNext_ + 1) % std::size(cache_);
    slot.page  = index;
    if (!codec_.Unpack(page.packed, page.rawSize, slot.text)) slot.text.clear();
    return slot.text;
}

std::string_view CommitStore::Text(const Located& at) const {
    const std::vector<Row>& rows = pages_[at.page].rows;
    if (at.slot >= rows.size()) return {};
    const std::string_view all   = PageText(at.page);
    const size_t           begin = rows[at.slot].text;
    const size_t           end   = (at.slot + 1 < rows.size())
                                       ? rows[at.slot + 1].text
                                       : all.size();
    if (begin > end || end > all.size()) return {};
    return all.substr(begin, end - begin);
}

std::wstring CommitStore::Hash(const Located& at) const {
    return EncodeHex<std::wstring>(
        pages_[at.page].hashes.data() + at.slot * hashLen_, hashLen_ * 2);
}

std::wstring CommitStore::Date(const Row& r, std::string_view text) const {
    if (r.date == kTextDate) {
        return Utf8ToWide(text.substr(0, std::min<size_t>(r.dateLen, text.size())));
    }
    return FormatDate<std::wstring>(r.date);
}

uint32_t CommitStore::Intern(std::string_view name, std::string_view email) {
    std::string key;
    key.reserve(name.size() + 1 + email.size());
    key.append(name);
    key += kPersonSeparator;
    key.append(email);
    auto [it, inserted] = personIndex_.try_emplace(
        std::move(key), static_cast<uint32_t>(people_.size()));
    if (inserted) people_.push_back(&it->first);
    return it->second;
}

}

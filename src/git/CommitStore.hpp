#pragma once

#include "git/Compression.hpp"
#include "git/Types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace git_tools {

class CommitStore {
public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    bool Append(std::string_view record);

    size_t size() const { return count_; }

    Commit       At(size_t i) const;
    std::wstring Sha(size_t i) const;
    bool         Subject(size_t i, std::wstring& out) const;
    size_t       IndexOf(std::wstring_view sha) const;

    void        SetResidentLimit(size_t pages);
    bool        NeedsRestore(size_t i) const;
    std::string Revisions(size_t i) const;
    bool        Restore(size_t i, std::string_view output);

private:
    struct Row {
        uint32_t text;
        uint32_t person;
        uint32_t date;
        uint8_t  abbrevLen;
        uint8_t  dateLen;
    };

    struct Page {
        std::vector<Row>     rows;
        std::vector<uint8_t> hashes;
        std::string          text;
        std::string          packed;
        uint32_t             rawSize       = 0;
        mutable uint64_t     lastUse       = 0;
        bool                 sealed        = false;
        bool                 unloaded      = false;
        bool                 restoreFailed = false;
    };

    struct Parsed {
        uint8_t          hash[32];
        size_t           hashLen = 0;
        std::string_view abbrev;
        std::string_view name;
        std::string_view email;
        std::string_view date;
        std::string_view subject;
    };

    struct Located {
        size_t page;
        size_t slot;
    };

    struct CachedPage {
        size_t      page = npos;
        std::string text;
    };

    static bool Parse(std::string_view record, Parsed& out);

    void             AddRow(Page& page, const Parsed& p);
    void             Seal(size_t index);
    void             Unload(size_t index);
    void             EnforceLimit();
    void             Forget(size_t index) const;
    void             Touch(size_t index) const;
    size_t           PageCount(const Page& page) const;
    Located          Locate(size_t i) const;
    std::string_view PageText(size_t index) const;
    std::string_view Text(const Located& at) const;
    std::wstring     Hash(const Located& at) const;
    std::wstring     Date(const Row& r, std::string_view text) const;
    uint32_t         Intern(std::string_view name, std::string_view email);

    std::vector<Page>                         pages_;
    std::unordered_map<std::string, uint32_t> personIndex_;
    std::vector<const std::string*>           people_;
    size_t                                    hashLen_       = 0;
    size_t                                    count_         = 0;
    size_t                                    residentLimit_ = 0;
    mutable TextCodec                         codec_;
    mutable CachedPage                        cache_[4];
    mutable size_t                            cacheNext_     = 0;
    mutable uint64_t                          tick_          = 0;
};

}

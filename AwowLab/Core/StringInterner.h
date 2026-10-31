#pragma once
#include <string>
#include <string_view>
#include <deque>
#include <unordered_map>
#include <cstdint>

// Stores each distinct string once and hands out a small integer id for it.
// Ids and the views returned by lookup() stay valid until clear().
class StringInterner {
public:
    using Id = uint32_t;
    static constexpr Id INVALID = 0;

    StringInterner();
    ~StringInterner() = default;

    StringInterner(const StringInterner&) = delete;
    StringInterner& operator=(const StringInterner&) = delete;
    StringInterner(StringInterner&&) = default;
    StringInterner& operator=(StringInterner&&) = default;

    // Returns the existing id, or adds the string. Not thread-safe.
    Id intern(std::string_view str);

    // Like intern() but never adds: INVALID if the string is unknown. For
    // read-only callers such as UI queries that must not grow the pool.
    Id find(std::string_view str) const;

    // Empty view for an unknown id
    std::string_view lookup(Id id) const;

    bool isValid(Id id) const;
    size_t size() const { return strings_.size(); }

    // Approximate, in bytes
    size_t memoryUsage() const;

    void clear();

private:
    // strings_[0] is the empty placeholder behind INVALID. A deque, because
    // index_ keys are views into these strings and push_back on a deque
    // never moves existing elements.
    std::deque<std::string> strings_;
    std::unordered_map<std::string_view, Id> index_;
};

// Process-wide pools. They are never cleared: ids stored in databases and
// views held by events have to stay valid across encounter loads.
StringInterner& guidInterner();
StringInterner& actorNameInterner();

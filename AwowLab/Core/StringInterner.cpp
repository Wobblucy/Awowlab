#include "StringInterner.h"

StringInterner::StringInterner() {
    strings_.emplace_back("");
}

StringInterner::Id StringInterner::intern(std::string_view str) {
    if (str.empty()) {
        return INVALID;
    }

    auto it = index_.find(str);
    if (it != index_.end()) {
        return it->second;
    }

    Id newId = static_cast<Id>(strings_.size());
    strings_.emplace_back(str);
    index_.emplace(std::string_view(strings_.back()), newId);

    return newId;
}

StringInterner::Id StringInterner::find(std::string_view str) const {
    if (str.empty()) {
        return INVALID;
    }
    auto it = index_.find(str);
    return (it != index_.end()) ? it->second : INVALID;
}

std::string_view StringInterner::lookup(Id id) const {
    if (id >= strings_.size()) {
        return {};
    }
    return strings_[id];
}

bool StringInterner::isValid(Id id) const {
    return id != INVALID && id < strings_.size();
}

size_t StringInterner::memoryUsage() const {
    size_t total = 0;

    total += sizeof(strings_) + strings_.size() * sizeof(std::string);

    for (const auto& s : strings_) {
        total += s.capacity();
    }

    total += sizeof(index_);
    total += index_.bucket_count() * sizeof(void*);
    total += index_.size() * (sizeof(std::string_view) + sizeof(Id) + sizeof(void*));

    return total;
}

void StringInterner::clear() {
    strings_.clear();
    index_.clear();
    strings_.emplace_back("");
}

namespace {
    StringInterner g_guidInterner;
    StringInterner g_actorNameInterner;
}

StringInterner& guidInterner() {
    return g_guidInterner;
}

StringInterner& actorNameInterner() {
    return g_actorNameInterner;
}

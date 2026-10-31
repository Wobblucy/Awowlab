#pragma once
#include <list>
#include <unordered_map>
#include <optional>
#include <functional>

// Generic LRU (Least Recently Used) cache with configurable eviction
// Template parameters:
//   Key - the key type (e.g., uint32_t for spell IDs)
//   Value - the value type (e.g., TextureData)
//   Deleter - a callable to clean up evicted values (e.g., destroy Vulkan resources)
template<typename Key, typename Value>
class LRUCache {
public:
    using EvictionCallback = std::function<void(const Key&, Value&)>;

    explicit LRUCache(size_t maxSize, EvictionCallback onEvict = nullptr)
        : maxSize_(maxSize)
        , onEvict_(std::move(onEvict))
    {
    }

    ~LRUCache() {
        clear();
    }

    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;

    LRUCache(LRUCache&&) = default;
    LRUCache& operator=(LRUCache&&) = default;

    // Get a value by key, returns nullptr if not found
    // Moves the item to front (most recently used) if found
    Value* get(const Key& key) {
        auto it = lookup_.find(key);
        if (it == lookup_.end()) {
            return nullptr;
        }

        // Move to front (most recently used)
        items_.splice(items_.begin(), items_, it->second);
        return &it->second->second;
    }

    // Get a const value by key, returns nullptr if not found
    // Does NOT update LRU order (const access)
    const Value* peek(const Key& key) const {
        auto it = lookup_.find(key);
        if (it == lookup_.end()) {
            return nullptr;
        }
        return &it->second->second;
    }

    // Check if key exists in cache
    bool contains(const Key& key) const {
        return lookup_.find(key) != lookup_.end();
    }

    // Insert or update a value
    // If cache is full, evicts least recently used item first
    // Replacing an existing key passes the old value to the eviction callback
    void put(const Key& key, Value value) {
        auto it = lookup_.find(key);

        if (it != lookup_.end()) {
            // Update existing - move to front and update value. The old value
            // goes through the eviction callback first: it may own resources
            // (a texture) that would otherwise leak.
            if (onEvict_) {
                onEvict_(it->second->first, it->second->second);
            }
            it->second->second = std::move(value);
            items_.splice(items_.begin(), items_, it->second);
            return;
        }

        // Evict if necessary
        while (items_.size() >= maxSize_ && !items_.empty()) {
            evictOne();
        }

        // Insert new item at front
        items_.emplace_front(key, std::move(value));
        lookup_[key] = items_.begin();
    }

    // Remove a specific key from the cache
    bool remove(const Key& key) {
        auto it = lookup_.find(key);
        if (it == lookup_.end()) {
            return false;
        }

        if (onEvict_) {
            onEvict_(it->second->first, it->second->second);
        }

        items_.erase(it->second);
        lookup_.erase(it);
        return true;
    }

    // Clear all items from the cache
    void clear() {
        if (onEvict_) {
            for (auto& item : items_) {
                onEvict_(item.first, item.second);
            }
        }
        items_.clear();
        lookup_.clear();
    }


    size_t size() const { return items_.size(); }


    size_t capacity() const { return maxSize_; }


    bool empty() const { return items_.empty(); }


    template<typename Func>
    void forEach(Func&& callback) {
        for (auto& item : items_) {
            callback(item.first, item.second);
        }
    }


    template<typename Func>
    void forEach(Func&& callback) const {
        for (const auto& item : items_) {
            callback(item.first, item.second);
        }
    }

private:
    void evictOne() {
        if (items_.empty()) return;

        // Evict from back (least recently used)
        auto& back = items_.back();
        if (onEvict_) {
            onEvict_(back.first, back.second);
        }
        lookup_.erase(back.first);
        items_.pop_back();
    }

    size_t maxSize_;
    EvictionCallback onEvict_;

    std::list<std::pair<Key, Value>> items_;

    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> lookup_;
};

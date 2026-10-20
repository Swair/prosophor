// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "voice_interaction/tts_audio_cache.h"

#include <iterator>
#include <utility>

namespace prosophor::voice {

namespace {

void HashCombine(size_t& seed, size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

}  // namespace

size_t TtsCacheKeyHash::operator()(const TtsCacheKey& key) const {
    size_t seed = std::hash<std::string>{}(key.text);
    HashCombine(seed, std::hash<std::string>{}(key.voice));
    HashCombine(seed, std::hash<std::string>{}(key.backend));
    HashCombine(seed, std::hash<int>{}(key.sample_rate));
    HashCombine(seed, std::hash<int>{}(key.channels));
    return seed;
}

TtsAudioCache::TtsAudioCache(size_t max_bytes) : max_bytes_(max_bytes) {}

size_t TtsAudioCache::EntryBytes(const TtsCacheKey& key, const TtsResponse& response) {
    size_t bytes = sizeof(Entry);
    bytes += key.text.capacity() + key.voice.capacity() + key.backend.capacity();
    bytes += response.error_msg.capacity();
    bytes += response.pcm.capacity() * sizeof(int16_t);
    return bytes;
}

bool TtsAudioCache::Get(const TtsCacheKey& key, TtsResponse& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_.find(key);
    if (it == index_.end()) {
        ++stats_.misses;
        return false;
    }
    entries_.splice(entries_.begin(), entries_, it->second);   // 刷新 LRU
    out = entries_.front().response;
    ++stats_.hits;
    return true;
}

void TtsAudioCache::Put(const TtsCacheKey& key, const TtsResponse& response) {
    if (!response.success || response.pcm.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (max_bytes_ == 0) {
        return;
    }

    const size_t incoming = EntryBytes(key, response);
    if (incoming > max_bytes_) {
        ++stats_.oversized;
        return;
    }

    // 同 key 先移除旧条目（重新插入到 LRU 头部）
    auto existing = index_.find(key);
    if (existing != index_.end()) {
        bytes_ -= existing->second->bytes;
        entries_.erase(existing->second);
        index_.erase(existing);
    }

    EvictUntilFitsLocked(incoming);
    entries_.push_front(Entry{key, response, incoming});
    index_[key] = entries_.begin();
    bytes_ += incoming;
    ++stats_.puts;
}

void TtsAudioCache::EvictUntilFitsLocked(size_t incoming_bytes) {
    while (!entries_.empty() && bytes_ + incoming_bytes > max_bytes_) {
        auto last = std::prev(entries_.end());
        bytes_ -= last->bytes;
        index_.erase(last->key);
        entries_.erase(last);
        ++stats_.evictions;
    }
}

void TtsAudioCache::SetMaxBytes(size_t max_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_bytes_ = max_bytes;
    EvictUntilFitsLocked(0);
}

void TtsAudioCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    index_.clear();
    bytes_ = 0;
}

size_t TtsAudioCache::bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

size_t TtsAudioCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

TtsAudioCache::Stats TtsAudioCache::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}  // namespace prosophor::voice

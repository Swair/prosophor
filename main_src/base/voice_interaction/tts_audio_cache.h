// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// sibot TtsCache 的内存版：文本 → 合成 PCM 的 LRU 缓存。
// 与 sibot 的无界 map + 磁盘缓存不同，这里只保留内存并设字节上限，
// 避免长期运行占满内存/磁盘；0 表示关闭缓存。

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

#include "providers/tts/tts_provider.h"

namespace prosophor::voice {

struct TtsCacheKey {
    std::string text;
    std::string voice;
    std::string backend;
    int sample_rate = 24000;
    int channels = 1;

    bool operator==(const TtsCacheKey& other) const {
        return text == other.text && voice == other.voice &&
               backend == other.backend && sample_rate == other.sample_rate &&
               channels == other.channels;
    }
};

struct TtsCacheKeyHash {
    size_t operator()(const TtsCacheKey& key) const;
};

class TtsAudioCache {
public:
    explicit TtsAudioCache(size_t max_bytes = 0);

    /// 命中时返回缓存副本并刷新 LRU；未命中返回 false
    bool Get(const TtsCacheKey& key, TtsResponse& out);

    /// 只缓存成功且 PCM 非空的结果；单条超过上限则拒收
    void Put(const TtsCacheKey& key, const TtsResponse& response);

    void SetMaxBytes(size_t max_bytes);
    void Clear();

    size_t bytes() const;
    size_t size() const;

    struct Stats {
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t puts = 0;
        uint64_t evictions = 0;
        uint64_t oversized = 0;
    };
    Stats stats() const;

private:
    struct Entry {
        TtsCacheKey key;
        TtsResponse response;
        size_t bytes = 0;
    };
    using EntryList = std::list<Entry>;

    static size_t EntryBytes(const TtsCacheKey& key, const TtsResponse& response);
    void EvictUntilFitsLocked(size_t incoming_bytes);

    mutable std::mutex mutex_;
    size_t max_bytes_ = 0;
    size_t bytes_ = 0;
    EntryList entries_;   // front = 最近使用
    std::unordered_map<TtsCacheKey, EntryList::iterator, TtsCacheKeyHash> index_;
    Stats stats_;
};

}  // namespace prosophor::voice

#include "source/common/common/assert.h"
#include "source/extensions/filters/network/redis_proxy/hotkey/cache/lfucache/lfu_cache.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace HotKey {
namespace Cache {
namespace LFUCache {

LFUCache::LFUCache(const uint8_t& capacity, const uint8_t& warming_capacity)
    : Cache(capacity, warming_capacity), freq_head_(std::make_shared<LFUCache::FreqNode>(this)),
      freq_tail_(std::make_shared<LFUCache::FreqNode>(this)) {
  freq_head_->next_ = freq_tail_;
  freq_tail_->prev_ = freq_head_;
  freq_head_->count_ = 0;
  freq_tail_->count_ = UINT32_MAX;
}

LFUCache::~LFUCache() { resetEx(); }

void LFUCache::touchKey(const std::string& key) {
  if (items_.count(key) > 0) {
    updateItem(key);
  } else {
    addItem(key);
  }
}

void LFUCache::incrKey(const std::string& key, const uint32_t& count) {
  if (items_.count(key) > 0) {
    updateItemIncr(key, count);
  } else {
    addItem(key, count);
  }
}

void LFUCache::setKey(const std::string& key, const uint32_t& count) {
  if (items_.count(key) > 0) {
    updateItem(key, count);
  } else {
    addItem(key, count);
  }
}

uint8_t LFUCache::getCache(absl::flat_hash_map<std::string, uint32_t>& cache) {
  LFUCache::ItemNodeSharedPtr item;
  LFUCache::FreqNodeSharedPtr freq = freq_tail_->prev_.lock();
  ASSERT(freq);
  cache.clear();
  while (freq != freq_head_) {
    item = freq->item_tail_->prev_.lock();
    ASSERT(item);
    while (item != freq->item_head_) {
      cache.emplace(std::make_pair(item->key_, freq->count_));
      if (cache.size() >= capacity_) {
        break;
      }
      item = item->prev_.lock();
      ASSERT(item);
    }
    if (cache.size() >= capacity_) {
      break;
    }
    freq = freq->prev_.lock();
    ASSERT(freq);
  }
  return cache.size();
}

void LFUCache::attenuate(const uint64_t& attenuate_time_ms) {
  std::chrono::time_point<std::chrono::high_resolution_clock> current_time(
      std::chrono::high_resolution_clock::now());
  LFUCache::FreqNodeSharedPtr freq = freq_head_->next_.lock();
  ASSERT(freq);
  LFUCache::FreqNodeSharedPtr freq_first = freq;
  LFUCache::FreqNodeSharedPtr freq_next;
  LFUCache::ItemNodeSharedPtr item;
  LFUCache::ItemNodeSharedPtr item_first;
  LFUCache::ItemNodeSharedPtr item_next;
  uint32_t count;
  // Keep the elimination order
  while ((freq != freq_tail_) && (freq_next != freq_first)) {
    freq_next = freq->next_.lock();
    count = freq->count_ >> 1;
    item = freq->item_head_->next_.lock();
    ASSERT(item);
    item_first = item;
    item_next = nullptr;
    while ((item != freq->item_tail_) && (item_next != item_first)) {
      item_next = item->next_.lock();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(current_time - item->last_time_)
              .count() >= static_cast<int64_t>(attenuate_time_ms)) {
        updateItemFreq(item, getFreqNodeByCount(count, freq), true);
      } else {
        updateItemFreq(item, freq, false);
      }
      item = item_next;
      ASSERT(item);
    }
    freq = freq_next;
    ASSERT(freq);
  }
}

LFUCache::FreqNode::FreqNode(LFUCache* lfu)
    : item_head_(std::make_shared<LFUCache::ItemNode>(lfu)),
      item_tail_(std::make_shared<LFUCache::ItemNode>(lfu)), lfu_(lfu) {
  item_head_->next_ = item_tail_;
  item_tail_->prev_ = item_head_;
  item_head_->key_ = fmt::format("{}_item_head", static_cast<void*>(this));
  item_tail_->key_ = fmt::format("{}_item_tail", static_cast<void*>(this));
}

LFUCache::FreqNode::~FreqNode() {
  freeSelf(false);
  item_head_ = nullptr;
  item_tail_ = nullptr;
}

void LFUCache::FreqNode::freeSelf(const bool& is_reuse) {
  LFUCache::FreqNodeSharedPtr prev = prev_.lock();
  LFUCache::FreqNodeSharedPtr next = next_.lock();
  if (prev) {
    prev->next_.swap(next_);
  }
  if (next) {
    next->prev_.swap(prev_);
  }
  item_head_->next_ = item_tail_;
  item_tail_->prev_ = item_head_;

  if (is_reuse) {
    ASSERT(lfu_ != nullptr);
    lfu_->free_freqs_.emplace_back(shared_from_this());
  }
}

void LFUCache::FreqNode::addItem(LFUCache::ItemNodeSharedPtr new_item) {
  LFUCache::ItemNodeSharedPtr prev = item_tail_->prev_.lock();
  ASSERT(prev);
  prev->next_ = new_item;
  item_tail_->prev_ = new_item;
  new_item->prev_ = prev;
  new_item->next_ = item_tail_;
}

bool LFUCache::FreqNode::popFront() {
  bool ret(false);
  LFUCache::ItemNodeSharedPtr item = item_head_->next_.lock();
  ASSERT(item);
  if (item != item_tail_) {
    item->freeSelf();
    ret = true;
  }
  return ret;
}

LFUCache::ItemNode::ItemNode(LFUCache* lfu)
    : last_time_(std::chrono::high_resolution_clock::now()), lfu_(lfu) {}

LFUCache::ItemNode::~ItemNode() { freeSelf(false); }

void LFUCache::ItemNode::freeSelf(const bool& is_reuse) {
  LFUCache::ItemNodeSharedPtr prev = prev_.lock();
  LFUCache::ItemNodeSharedPtr next = next_.lock();
  if (prev) {
    prev->next_.swap(next_);
  }
  if (next) {
    next->prev_.swap(prev_);
  }
  if (freq_) {
    LFUCache::ItemNodeSharedPtr next = freq_->item_head_->next_.lock();
    ASSERT(next);
    if (next == freq_->item_tail_) {
      freq_->freeSelf();
    }
    freq_ = nullptr;
  }

  if (is_reuse) {
    ASSERT(lfu_ != nullptr);
    lfu_->items_.erase(key_);
    lfu_->free_items_.emplace_back(shared_from_this());
  }
}

inline void LFUCache::updateItem(const std::string& key) {
  LFUCache::ItemNodeSharedPtr item = items_.at(key);
  updateItemEx(item);
}

inline void LFUCache::updateItemEx(LFUCache::ItemNodeSharedPtr item) {
  LFUCache::FreqNodeSharedPtr freq = item->freq_;
  uint32_t count = freq->count_ + ((freq->count_ < UINT32_MAX) ? 1 : 0);
  updateItemFreq(item, getFreqNodeByCount(count, freq));
}

inline void LFUCache::updateItem(const std::string& key, const uint32_t& count) {
  LFUCache::ItemNodeSharedPtr item = items_.at(key);
  updateItemEx(item, count);
}

inline void LFUCache::updateItemEx(LFUCache::ItemNodeSharedPtr item, const uint32_t& count) {
  LFUCache::FreqNodeSharedPtr freq = item->freq_;
  updateItemFreq(item, getFreqNodeByCount(count, freq));
}

inline void LFUCache::updateItemIncr(const std::string& key, const uint32_t& count) {
  LFUCache::ItemNodeSharedPtr item = items_.at(key);
  updateItemIncrEx(item, count);
}

inline void LFUCache::updateItemIncrEx(LFUCache::ItemNodeSharedPtr item, uint32_t count) {
  LFUCache::FreqNodeSharedPtr freq = item->freq_;
  count = ((UINT32_MAX - count) > freq->count_) ? (count + freq->count_) : UINT32_MAX;
  updateItemFreq(item, getFreqNodeByCount(count, freq));
}

void LFUCache::addItem(const std::string& key, const uint32_t& count) {
  if (count <= 0) {
    return;
  }
  if (items_.size() >= (capacity_ + warming_capacity_)) {
    eliminate();
  }
  LFUCache::ItemNodeSharedPtr item;
  if (free_items_.empty()) {
    item = std::make_shared<LFUCache::ItemNode>(this);
  } else {
    item = free_items_.front();
    free_items_.pop_front();
  }
  item->key_ = key;
  updateItemFreq(item, getFreqNodeByCount(count));
  items_.emplace(std::make_pair(key, item));
}

void LFUCache::eliminate() {
  LFUCache::FreqNodeSharedPtr freq = freq_head_->next_.lock();
  ASSERT(freq);
  while (freq != freq_tail_) {
    if (!freq->popFront()) {
      freq = freq->next_.lock();
      ASSERT(freq);
      continue;
    } else {
      break;
    }
  }
}

LFUCache::FreqNodeSharedPtr LFUCache::getFreqNodeByCount(const uint32_t& count,
                                                         LFUCache::FreqNodeSharedPtr benchmark) {
  if (count <= 0) {
    return nullptr;
  }
  if (benchmark) {
    if (count == benchmark->count_) {
      return benchmark;
    }
  } else {
    benchmark = freq_head_;
  }
  LFUCache::FreqNodeSharedPtr new_freq;
  LFUCache::FreqNodeSharedPtr prev, next;
  while (benchmark) {
    if (benchmark == freq_head_) {
      next = benchmark->next_.lock();
      ASSERT(next);
      if (next == freq_tail_) {
        new_freq = nullptr;
        prev = freq_head_;
        break;
      } else {
        benchmark = next;
        continue;
      }
    }

    if (benchmark == freq_tail_) {
      prev = benchmark->prev_.lock();
      ASSERT(prev);
      if (prev == freq_head_) {
        new_freq = nullptr;
        next = freq_tail_;
        break;
      } else {
        benchmark = prev;
        continue;
      }
    }

    prev = benchmark->prev_.lock();
    next = benchmark->next_.lock();
    ASSERT(prev);
    ASSERT(next);
    if (benchmark->count_ == count) {
      new_freq = benchmark;
      break;
    } else if (benchmark->count_ > count) {
      if ((prev->count_ < count) || (prev == freq_head_)) {
        new_freq = nullptr;
        next = benchmark;
        break;
      } else {
        benchmark = prev;
        continue;
      }
    } else {
      if ((next->count_ > count) || (next == freq_tail_)) {
        new_freq = nullptr;
        prev = benchmark;
        break;
      } else {
        benchmark = next;
        continue;
      }
    }
  }
  if (new_freq == nullptr) {
    if (free_freqs_.empty()) {
      new_freq = std::make_shared<LFUCache::FreqNode>(this);
    } else {
      new_freq = free_freqs_.front();
      free_freqs_.pop_front();
    }
    new_freq->count_ = count;
  }
  new_freq->prev_ = prev;
  new_freq->next_ = next;
  next->prev_ = new_freq;
  prev->next_ = new_freq;
  return new_freq;
}

void LFUCache::updateItemFreq(LFUCache::ItemNodeSharedPtr new_item,
                              LFUCache::FreqNodeSharedPtr new_freq,
                              const bool& update_time_enable) {
  if (new_freq) {
    if ((new_item->prev_.lock() != new_freq->item_head_) ||
        (new_item->next_.lock() != new_freq->item_tail_)) {
      new_item->freeSelf(false);
      new_item->freq_ = new_freq;
      new_item->freq_->addItem(new_item);
    }

    if (update_time_enable) {
      new_item->last_time_ = std::chrono::high_resolution_clock::now();
    }
  } else {
    new_item->freeSelf(true);
  }
}

void LFUCache::resetEx() {
  while (!items_.empty()) {
    eliminate();
  }
}

} // namespace LFUCache
} // namespace Cache
} // namespace HotKey
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

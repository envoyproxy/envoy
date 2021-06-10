#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <string>

#include "extensions/filters/network/redis_proxy/hotkey/cache/cache.h"

#include "absl/base/attributes.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace HotKey {
namespace Cache {
namespace LFUCache {

class LFUCache : public Cache {
public:
  LFUCache(const uint8_t& capacity, const uint8_t& warming_capacity = 5);
  ~LFUCache() override;

  void reset() override { resetEx(); }
  void touchKey(const std::string& key) override;
  void incrKey(const std::string& key, const uint32_t& count) override;
  void setKey(const std::string& key, const uint32_t& count) override;
  uint8_t getCache(absl::flat_hash_map<std::string, uint32_t>& cache) override;
  void attenuate(const uint64_t& attenuate_time_ms = 0) override;

private:
  class ItemNode;
  using ItemNodeSharedPtr = std::shared_ptr<LFUCache::ItemNode>;

  class FreqNode : public std::enable_shared_from_this<LFUCache::FreqNode> {
  public:
    FreqNode(LFUCache* lfu);
    ~FreqNode();

    void addItem(LFUCache::ItemNodeSharedPtr new_item);
    bool popFront();
    void freeSelf(const bool& is_reuse = true);

    uint32_t count_;
    std::weak_ptr<LFUCache::FreqNode> prev_, next_;
    LFUCache::ItemNodeSharedPtr item_head_, item_tail_;
    LFUCache* lfu_;
  };
  using FreqNodeSharedPtr = std::shared_ptr<LFUCache::FreqNode>;

  class ItemNode : public std::enable_shared_from_this<LFUCache::ItemNode> {
  public:
    ItemNode(LFUCache* lfu);
    ~ItemNode();

    void freeSelf(const bool& is_reuse = true);

    std::string key_;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_time_;
    std::weak_ptr<LFUCache::ItemNode> prev_, next_;
    LFUCache::FreqNodeSharedPtr freq_;
    LFUCache* lfu_;
  };

  void updateItem(const std::string& key);
  void updateItemEx(LFUCache::ItemNodeSharedPtr item);
  void updateItem(const std::string& key, const uint32_t& count);
  void updateItemEx(LFUCache::ItemNodeSharedPtr item, const uint32_t& count);
  void updateItemIncr(const std::string& key, const uint32_t& count);
  void updateItemIncrEx(LFUCache::ItemNodeSharedPtr item, uint32_t count);
  void addItem(const std::string& key, const uint32_t& count = 1);
  void eliminate();
  ABSL_MUST_USE_RESULT LFUCache::FreqNodeSharedPtr
  getFreqNodeByCount(const uint32_t& count, LFUCache::FreqNodeSharedPtr benchmark = nullptr);
  static void updateItemFreq(LFUCache::ItemNodeSharedPtr new_item,
                             LFUCache::FreqNodeSharedPtr new_freq,
                             const bool& update_time_enable = true);
  void resetEx();

  absl::flat_hash_map<std::string, LFUCache::ItemNodeSharedPtr> items_;
  std::list<LFUCache::ItemNodeSharedPtr> free_items_;
  std::list<LFUCache::FreqNodeSharedPtr> free_freqs_;
  LFUCache::FreqNodeSharedPtr freq_head_, freq_tail_;
};
using LFUCacheSharedPtr = std::shared_ptr<LFUCache>;

} // namespace LFUCache
} // namespace Cache
} // namespace HotKey
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

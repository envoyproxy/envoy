#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"

#include "source/extensions/filters/network/redis_proxy/hotkey/cache/cache_factory.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace HotKey {

class HotKeyCounter {
  friend class HotKeyCounterTest;

public:
  HotKeyCounter(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey::CacheType&
          hotkey_cache_type,
      const uint8_t& cache_capacity);
  ~HotKeyCounter() = default;

  const std::string name() { return name_; }
  inline uint8_t getHotKeys(absl::flat_hash_map<std::string, uint32_t>& hotkeys) {
    Thread::LockGuard lock_guard(hotkey_cache_mutex_);
    return hotkey_cache_->getCache(hotkeys);
  }
  inline void reset() {
    Thread::LockGuard lock_guard(hotkey_cache_mutex_);
    hotkey_cache_->reset();
  }
  void incr(const std::string& key);

private:
  Cache::CacheSharedPtr hotkey_cache_;
  Thread::MutexBasicLockable hotkey_cache_mutex_;
  const std::string name_;
};
using HotKeyCounterSharedPtr = std::shared_ptr<HotKeyCounter>;

/**
 * All redis proxy stats. @see stats_macros.h
 */
#define ALL_REDIS_HOTKEY_COLLECTOR_STATS(GAUGE)                                                    \
  GAUGE(active_counter, NeverImport)                                                               \
  GAUGE(draining_counter, NeverImport)                                                             \
  GAUGE(hotkey, NeverImport)                                                                       \
  GAUGE(hotkey_freq_avg, NeverImport)                                                              \
  GAUGE(hotkey_heat_avg, NeverImport)

/**
 * Struct definition for all redis hotkey collector stats. @see stats_macros.h
 */
struct HotKeyCollectorStats {
  ALL_REDIS_HOTKEY_COLLECTOR_STATS(GENERATE_GAUGE_STRUCT)
};

class HotKeyCollector {
public:
  HotKeyCollector(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey& config,
      Event::Dispatcher& dispatcher, const std::string& prefix, Stats::Scope& scope);
  ~HotKeyCollector();

  HotKeyCounterSharedPtr createHotKeyCounter();
  void destroyHotKeyCounter(HotKeyCounterSharedPtr& counter);
  void run();
  inline uint8_t getHotKeys(absl::flat_hash_map<std::string, uint32_t>& hotkeys) {
    Thread::LockGuard lock_guard(hotkey_cache_mutex_);
    return hotkey_cache_->getCache(hotkeys);
  }
  uint8_t getHotKeyHeats(absl::flat_hash_map<std::string, uint32_t>& hotkeys);

private:
  void registerHotKeyCounter(const HotKeyCounterSharedPtr& counter);
  void unregisterHotKeyCounter(const HotKeyCounterSharedPtr& counter);
  void collect();
  void attenuate();
  static HotKeyCollectorStats generateHotKeyCollectorStats(const std::string& prefix,
                                                           Stats::Scope& scope);

  Event::Dispatcher& dispatcher_;
  Event::TimerPtr collect_timer_;
  Event::TimerPtr attenuate_timer_;
  Cache::CacheSharedPtr hotkey_cache_;
  Thread::MutexBasicLockable hotkey_cache_mutex_;
  absl::flat_hash_map<std::string, HotKeyCounterSharedPtr> active_counters_;
  absl::flat_hash_map<std::string, HotKeyCounterSharedPtr> draining_counters_;
  Thread::MutexBasicLockable counters_mutex_;
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey::CacheType
      hotkey_cache_type_;
  uint32_t hotkey_cache_capacity_;
  uint64_t collect_dispatch_interval_ms_;
  uint64_t attenuate_dispatch_interval_ms_;
  uint64_t attenuate_cache_interval_ms_;
  const std::string prefix_;
  Stats::Scope& scope_;
  HotKeyCollectorStats hotkey_collector_stats_;
};
using HotKeyCollectorSharedPtr = std::shared_ptr<HotKeyCollector>;

} // namespace HotKey
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

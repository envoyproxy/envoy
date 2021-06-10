#include "extensions/filters/network/redis_proxy/hotkey/hotkey_impl.h"

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace HotKey {

namespace {
constexpr double_t HOTKEY_UINT32_MAX_LOG_BASE_NUM(1.090878327);
const double_t HOTKEY_UINT32_MAX_LOG_BASE_LOG_NUM(std::log2(HOTKEY_UINT32_MAX_LOG_BASE_NUM));
constexpr uint8_t HOTKEY_DEFAULT_CACHE_CAPACITY(32);
constexpr uint64_t HOTKEY_DEFAULT_COLLECT_DISPATCH_INTERVAL_MS(5 * 1000);
constexpr uint64_t HOTKEY_DEFAULT_ATTENUATE_DISPATCH_INTERVAL_MS(30 * 1000);
constexpr uint64_t HOTKEY_DEFAULT_ATTENUATE_CACHE_INTERVAL_MS(60 * 1000);
} // namespace

HotKeyCounter::HotKeyCounter(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey::CacheType&
        hotkey_cache_type,
    const uint8_t& hotkey_cache_capacity)
    : hotkey_cache_(Cache::CacheFactory::createCache(hotkey_cache_type, hotkey_cache_capacity)),
      name_(fmt::format("{}_HotKeyCounter", static_cast<void*>(this))) {}

void HotKeyCounter::incr(const std::string& key) {
  Thread::TryLockGuard try_lock_guard(hotkey_cache_mutex_);
  if (try_lock_guard.tryLock()) {
    hotkey_cache_->touchKey(key);
  }
}

HotKeyCollector::HotKeyCollector(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HotKey& config,
    Event::Dispatcher& dispatcher, const std::string& prefix, Stats::Scope& scope)
    : dispatcher_(dispatcher), hotkey_cache_type_(config.cache_type()),
      hotkey_cache_capacity_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, cache_capacity, HOTKEY_DEFAULT_CACHE_CAPACITY)),
      collect_dispatch_interval_ms_(PROTOBUF_GET_MS_OR_DEFAULT(
          config, collect_dispatch_interval, HOTKEY_DEFAULT_COLLECT_DISPATCH_INTERVAL_MS)),
      attenuate_dispatch_interval_ms_(PROTOBUF_GET_MS_OR_DEFAULT(
          config, attenuate_dispatch_interval, HOTKEY_DEFAULT_ATTENUATE_DISPATCH_INTERVAL_MS)),
      attenuate_cache_interval_ms_(PROTOBUF_GET_MS_OR_DEFAULT(
          config, attenuate_cache_interval, HOTKEY_DEFAULT_ATTENUATE_CACHE_INTERVAL_MS)),
      prefix_(prefix + "hotkey"), scope_(scope),
      hotkey_collector_stats_(generateHotKeyCollectorStats(prefix_, scope_)) {
  hotkey_cache_ = Cache::CacheFactory::createCache(hotkey_cache_type_, hotkey_cache_capacity_);
}

HotKeyCollector::~HotKeyCollector() {
  hotkey_collector_stats_.active_counter_.set(0);
  hotkey_collector_stats_.draining_counter_.set(0);
}

HotKeyCounterSharedPtr HotKeyCollector::createHotKeyCounter() {
  HotKeyCounterSharedPtr counter(
      std::make_unique<HotKeyCounter>(hotkey_cache_type_, hotkey_cache_capacity_));
  registerHotKeyCounter(counter);
  return counter;
}

void HotKeyCollector::destroyHotKeyCounter(HotKeyCounterSharedPtr& counter) {
  unregisterHotKeyCounter(counter);
  counter = nullptr;
}

void HotKeyCollector::run() {
  collect_timer_ = dispatcher_.createTimer([this]() -> void {
    this->collect();
    collect_timer_->enableTimer(std::chrono::milliseconds(collect_dispatch_interval_ms_));
  });
  collect_timer_->enableTimer(std::chrono::milliseconds(collect_dispatch_interval_ms_));

  attenuate_timer_ = dispatcher_.createTimer([this]() -> void {
    this->attenuate();
    attenuate_timer_->enableTimer(std::chrono::milliseconds(attenuate_dispatch_interval_ms_));
  });
  attenuate_timer_->enableTimer(std::chrono::milliseconds(attenuate_dispatch_interval_ms_));
}

uint8_t HotKeyCollector::getHotKeyHeats(absl::flat_hash_map<std::string, uint32_t>& hotkeys) {
  uint8_t count(getHotKeys(hotkeys));
  uint64_t freq_sum(0);
  uint16_t heat_sum(0);

  for (auto& it : hotkeys) {
    freq_sum += it.second;
    if (it.second > 44) {
      // when it.second <= 44, the result of this algorithm may be >= it.second.
      it.second = std::ceil(std::log2(it.second) / HOTKEY_UINT32_MAX_LOG_BASE_LOG_NUM);
    }
    heat_sum += it.second;
  }

  hotkey_collector_stats_.hotkey_.set(count);
  hotkey_collector_stats_.hotkey_freq_avg_.set((count == 0) ? 0 : (freq_sum / count));
  hotkey_collector_stats_.hotkey_heat_avg_.set((count == 0) ? 0 : (heat_sum / count));
  return count;
}

inline void HotKeyCollector::registerHotKeyCounter(const HotKeyCounterSharedPtr& counter) {
  if (counter) {
    Thread::LockGuard lock_guard(counters_mutex_);
    active_counters_.emplace(std::make_pair(counter->name(), counter));
    hotkey_collector_stats_.active_counter_.inc();
  }
}

inline void HotKeyCollector::unregisterHotKeyCounter(const HotKeyCounterSharedPtr& counter) {
  if (counter) {
    Thread::LockGuard lock_guard(counters_mutex_);
    draining_counters_.emplace(std::make_pair(counter->name(), counter));
    hotkey_collector_stats_.draining_counter_.inc();
    active_counters_.erase(counter->name());
    hotkey_collector_stats_.active_counter_.dec();
  }
}

void HotKeyCollector::collect() {
  absl::flat_hash_map<std::string, uint32_t> hotkeys;
  absl::flat_hash_map<std::string, uint32_t> tmp_hotkeys;

  {
    Thread::LockGuard lock_guard(counters_mutex_);
    for (auto& it : active_counters_) {
      if (it.second->getHotKeys(tmp_hotkeys) > 0) {
        for (auto& tmp_it : tmp_hotkeys) {
          auto tmp_tmp_it = hotkeys.find(tmp_it.first);
          if (tmp_tmp_it == hotkeys.end()) {
            hotkeys[tmp_it.first] = tmp_it.second;
          } else {
            hotkeys[tmp_it.first] = ((UINT32_MAX - tmp_it.second) > tmp_tmp_it->second)
                                        ? (tmp_it.second + tmp_tmp_it->second)
                                        : UINT32_MAX;
          }
        }
      }
      it.second->reset();
    }
    for (auto& it : draining_counters_) {
      if (it.second->getHotKeys(tmp_hotkeys) > 0) {
        for (auto& tmp_it : tmp_hotkeys) {
          auto tmp_tmp_it = hotkeys.find(tmp_it.first);
          if (tmp_tmp_it == hotkeys.end()) {
            hotkeys[tmp_it.first] = tmp_it.second;
          } else {
            hotkeys[tmp_it.first] = ((UINT32_MAX - tmp_it.second) > tmp_tmp_it->second)
                                        ? (tmp_it.second + tmp_tmp_it->second)
                                        : UINT32_MAX;
          }
        }
      }
    }
    draining_counters_.clear();
    hotkey_collector_stats_.draining_counter_.set(0);
  }

  {
    Thread::LockGuard lock_guard(hotkey_cache_mutex_);
    for (auto& it : hotkeys) {
      hotkey_cache_->incrKey(it.first, it.second);
    }
  }

  getHotKeyHeats(tmp_hotkeys);
}

void HotKeyCollector::attenuate() {
  {
    Thread::LockGuard lock_guard(hotkey_cache_mutex_);
    hotkey_cache_->attenuate(attenuate_cache_interval_ms_);
  }

  absl::flat_hash_map<std::string, uint32_t> tmp_hotkeys;
  getHotKeyHeats(tmp_hotkeys);
}

HotKeyCollectorStats HotKeyCollector::generateHotKeyCollectorStats(const std::string& prefix,
                                                                   Stats::Scope& scope) {
  return {ALL_REDIS_HOTKEY_COLLECTOR_STATS(POOL_GAUGE_PREFIX(scope, (prefix + ".collector")))};
}

} // namespace HotKey
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

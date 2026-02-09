#include "source/extensions/dynamic_modules/module_cache.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

namespace {
// Global module cache instance.
std::unique_ptr<DynamicModuleCache> global_module_cache;
Thread::MutexBasicLockable global_module_cache_mutex;
} // namespace

DynamicModuleCache& getModuleCache() {
  Thread::LockGuard guard(global_module_cache_mutex);
  if (!global_module_cache) {
    global_module_cache = std::make_unique<DynamicModuleCache>();
  }
  return *global_module_cache;
}

void clearModuleCacheForTesting() {
  Thread::LockGuard guard(global_module_cache_mutex);
  if (global_module_cache) {
    global_module_cache->clear();
  }
}

void setTimeOffsetForModuleCacheForTesting(MonotonicTime::duration d) {
  Thread::LockGuard guard(global_module_cache_mutex);
  if (global_module_cache) {
    global_module_cache->setTimeOffsetForTesting(d);
  }
}

CacheLookupResult DynamicModuleCache::lookup(const std::string& key, MonotonicTime now) {
  Thread::LockGuard guard(mutex_);

  // Apply time offset for testing.
  now += time_offset_for_testing_;

  // Remove expired entries.
  removeExpiredEntries(now);

  auto it = cache_.find(key);
  if (it == cache_.end()) {
    return CacheLookupResult{nullptr, false, false};
  }

  ModuleCacheEntry& entry = it->second;
  entry.use_time = now;

  if (entry.in_progress) {
    return CacheLookupResult{nullptr, true, true};
  }

  // Check if this is a negative cache entry (no module data from a recent failed fetch).
  if (!entry.module) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.fetch_time).count();
    if (elapsed < NEGATIVE_CACHE_SECONDS) {
      // Still within negative cache TTL - return empty but mark as cache hit.
      return CacheLookupResult{nullptr, false, true};
    }
    // Negative cache expired - treat as cache miss.
    cache_.erase(it);
    return CacheLookupResult{nullptr, false, false};
  }

  // Check TTL for positive cache entries.
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.fetch_time).count();
  if (elapsed >= CACHE_TTL_SECONDS) {
    cache_.erase(it);
    return CacheLookupResult{nullptr, false, false};
  }

  return CacheLookupResult{entry.module, false, true};
}

void DynamicModuleCache::markInProgress(const std::string& key, MonotonicTime now) {
  Thread::LockGuard guard(mutex_);
  now += time_offset_for_testing_;

  ModuleCacheEntry& entry = cache_[key];
  entry.in_progress = true;
  entry.use_time = now;
  entry.fetch_time = now;
}

void DynamicModuleCache::update(const std::string& key, const std::string& module,
                                MonotonicTime now) {
  Thread::LockGuard guard(mutex_);
  now += time_offset_for_testing_;

  ModuleCacheEntry& entry = cache_[key];
  entry.module = module.empty() ? nullptr : std::make_shared<const std::string>(module);
  entry.in_progress = false;
  entry.use_time = now;
  entry.fetch_time = now;
}

size_t DynamicModuleCache::size() const {
  Thread::LockGuard guard(mutex_);
  return cache_.size();
}

void DynamicModuleCache::clear() {
  Thread::LockGuard guard(mutex_);
  cache_.clear();
  time_offset_for_testing_ = MonotonicTime::duration{};
}

void DynamicModuleCache::setTimeOffsetForTesting(MonotonicTime::duration offset) {
  Thread::LockGuard guard(mutex_);
  time_offset_for_testing_ = offset;
}

void DynamicModuleCache::removeExpiredEntries(MonotonicTime now) {
  // Called with mutex held.
  for (auto it = cache_.begin(); it != cache_.end();) {
    const ModuleCacheEntry& entry = it->second;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.fetch_time).count();

    bool expired = false;
    if (entry.in_progress) {
      // Evict in-progress entries that have been stuck longer than the timeout.
      // This prevents a hung fetch from permanently blocking a SHA256 key.
      expired = elapsed >= IN_PROGRESS_TIMEOUT_SECONDS;
    } else if (!entry.module) {
      // Negative cache entry.
      expired = elapsed >= NEGATIVE_CACHE_SECONDS;
    } else {
      // Positive cache entry.
      expired = elapsed >= CACHE_TTL_SECONDS;
    }

    if (expired) {
      cache_.erase(it++);
    } else {
      ++it;
    }
  }
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy

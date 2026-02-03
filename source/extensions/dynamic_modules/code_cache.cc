#include "source/extensions/dynamic_modules/code_cache.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

namespace {
// Global code cache instance.
std::unique_ptr<DynamicModuleCodeCache> global_code_cache;
Thread::MutexBasicLockable global_code_cache_mutex;
} // namespace

DynamicModuleCodeCache& getCodeCache() {
  Thread::LockGuard guard(global_code_cache_mutex);
  if (!global_code_cache) {
    global_code_cache = std::make_unique<DynamicModuleCodeCache>();
  }
  return *global_code_cache;
}

void clearCodeCacheForTesting() {
  Thread::LockGuard guard(global_code_cache_mutex);
  if (global_code_cache) {
    global_code_cache->clear();
  }
}

void setTimeOffsetForCodeCacheForTesting(MonotonicTime::duration d) {
  Thread::LockGuard guard(global_code_cache_mutex);
  if (global_code_cache) {
    global_code_cache->setTimeOffsetForTesting(d);
  }
}

CacheLookupResult DynamicModuleCodeCache::lookup(const std::string& key, MonotonicTime now) {
  Thread::LockGuard guard(mutex_);

  // Apply time offset for testing.
  now += time_offset_for_testing_;

  // Remove expired entries.
  removeExpiredEntries(now);

  auto it = cache_.find(key);
  if (it == cache_.end()) {
    return CacheLookupResult{"", false, false};
  }

  CodeCacheEntry& entry = it->second;
  entry.use_time = now;

  if (entry.in_progress) {
    return CacheLookupResult{"", true, true};
  }

  // Check if this is a negative cache entry (empty code with recent fetch).
  if (entry.code.empty()) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.fetch_time).count();
    if (elapsed < NEGATIVE_CACHE_SECONDS) {
      // Still within negative cache TTL - return empty but mark as cache hit.
      return CacheLookupResult{"", false, true};
    }
    // Negative cache expired - treat as cache miss.
    cache_.erase(it);
    return CacheLookupResult{"", false, false};
  }

  // Check TTL for positive cache entries.
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.fetch_time).count();
  if (elapsed >= CACHE_TTL_SECONDS) {
    cache_.erase(it);
    return CacheLookupResult{"", false, false};
  }

  return CacheLookupResult{entry.code, false, true};
}

void DynamicModuleCodeCache::markInProgress(const std::string& key, MonotonicTime now) {
  Thread::LockGuard guard(mutex_);
  now += time_offset_for_testing_;

  CodeCacheEntry& entry = cache_[key];
  entry.in_progress = true;
  entry.use_time = now;
  entry.fetch_time = now;
}

void DynamicModuleCodeCache::update(const std::string& key, const std::string& code,
                                    MonotonicTime now) {
  Thread::LockGuard guard(mutex_);
  now += time_offset_for_testing_;

  CodeCacheEntry& entry = cache_[key];
  entry.code = code;
  entry.in_progress = false;
  entry.use_time = now;
  entry.fetch_time = now;
}

size_t DynamicModuleCodeCache::size() const {
  Thread::LockGuard guard(mutex_);
  return cache_.size();
}

void DynamicModuleCodeCache::clear() {
  Thread::LockGuard guard(mutex_);
  cache_.clear();
  time_offset_for_testing_ = MonotonicTime::duration{};
}

void DynamicModuleCodeCache::setTimeOffsetForTesting(MonotonicTime::duration offset) {
  Thread::LockGuard guard(mutex_);
  time_offset_for_testing_ = offset;
}

void DynamicModuleCodeCache::removeExpiredEntries(MonotonicTime now) {
  // Called with mutex held.
  for (auto it = cache_.begin(); it != cache_.end();) {
    const CodeCacheEntry& entry = it->second;

    // Don't remove in-progress entries.
    if (entry.in_progress) {
      ++it;
      continue;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.fetch_time).count();

    bool expired = false;
    if (entry.code.empty()) {
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

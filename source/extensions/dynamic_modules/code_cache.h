#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/init/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/remote_data_fetcher.h"
#include "source/common/init/target_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * Represents an entry in the code cache.
 */
struct CodeCacheEntry {
  std::string code;         // Module binary data.
  bool in_progress;         // Fetch is ongoing.
  MonotonicTime use_time;   // Last access time.
  MonotonicTime fetch_time; // When the module was fetched.
};

/**
 * Result of a cache lookup operation.
 */
struct CacheLookupResult {
  // The cached code if found and valid, empty string otherwise.
  std::string code;
  // True if a fetch operation is already in progress for this key.
  bool fetch_in_progress;
  // True if a cache entry exists (even if in_progress or expired for negative cache).
  bool cache_hit;
};

/**
 * Callback invoked when async fetch completes.
 * @param code The fetched module bytes, or empty string on failure.
 */
using FetchCallback = std::function<void(const std::string& code)>;

/**
 * Thread-safe code cache for dynamic modules.
 *
 * Features:
 * - Keyed by SHA256 hash of module content.
 * - 24-hour TTL for cached entries.
 * - 10-second negative caching for failed fetches.
 * - In-progress tracking to avoid duplicate fetches.
 */
class DynamicModuleCodeCache : public Logger::Loggable<Logger::Id::config> {
public:
  // Cache TTL in seconds (24 hours).
  static constexpr int CACHE_TTL_SECONDS = 24 * 3600;
  // Negative cache TTL in seconds (10 seconds).
  static constexpr int NEGATIVE_CACHE_SECONDS = 10;

  DynamicModuleCodeCache() = default;
  ~DynamicModuleCodeCache() = default;

  /**
   * Looks up an entry in the cache.
   * @param key The SHA256 hash key.
   * @param now Current monotonic time for TTL checks.
   * @return CacheLookupResult containing the lookup results.
   */
  CacheLookupResult lookup(const std::string& key, MonotonicTime now);

  /**
   * Marks a cache entry as in-progress for fetching.
   * @param key The SHA256 hash key.
   * @param now Current monotonic time.
   */
  void markInProgress(const std::string& key, MonotonicTime now);

  /**
   * Updates a cache entry with fetched code.
   * @param key The SHA256 hash key.
   * @param code The fetched module bytes (empty string indicates fetch failure).
   * @param now Current monotonic time.
   */
  void update(const std::string& key, const std::string& code, MonotonicTime now);

  /**
   * Returns the current number of entries in the cache.
   */
  size_t size() const;

  /**
   * Clears the cache. Primarily for testing.
   */
  void clear();

  /**
   * Sets a time offset for testing TTL behavior.
   */
  void setTimeOffsetForTesting(MonotonicTime::duration offset);

private:
  // Removes expired entries during lookup.
  void removeExpiredEntries(MonotonicTime now);

  mutable Thread::MutexBasicLockable mutex_;
  absl::flat_hash_map<std::string, CodeCacheEntry> cache_;
  MonotonicTime::duration time_offset_for_testing_{};
};

/**
 * Singleton accessor for the global code cache.
 */
DynamicModuleCodeCache& getCodeCache();

/**
 * Clears the code cache. Primarily for testing.
 */
void clearCodeCacheForTesting();

/**
 * Sets a time offset for the code cache. Primarily for testing.
 */
void setTimeOffsetForCodeCacheForTesting(MonotonicTime::duration d);

/**
 * Adapter for remote data fetching that integrates with the code cache.
 */
class RemoteDataFetcherAdapter : public Config::DataFetcher::RemoteDataFetcherCallback,
                                 public Event::DeferredDeletable {
public:
  explicit RemoteDataFetcherAdapter(FetchCallback cb) : callback_(std::move(cb)) {}
  ~RemoteDataFetcherAdapter() override = default;

  // Config::DataFetcher::RemoteDataFetcherCallback
  void onSuccess(const std::string& data) override { callback_(data); }
  void onFailure(Config::DataFetcher::FailureReason) override { callback_(""); }

  void setFetcher(std::unique_ptr<Config::DataFetcher::RemoteDataFetcher>&& fetcher) {
    fetcher_ = std::move(fetcher);
  }

private:
  FetchCallback callback_;
  std::unique_ptr<Config::DataFetcher::RemoteDataFetcher> fetcher_;
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy

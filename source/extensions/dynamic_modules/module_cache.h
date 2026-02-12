#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/event/deferred_deletable.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/remote_data_fetcher.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * Represents an entry in the module cache.
 */
struct ModuleCacheEntry {
  std::shared_ptr<const std::string>
      module;               // Module binary data (nullptr for negative/in-progress).
  bool in_progress{false};  // Fetch is ongoing.
  MonotonicTime use_time;   // Last access time.
  MonotonicTime fetch_time; // When the module was fetched.
};

/**
 * Result of a cache lookup operation.
 */
struct CacheLookupResult {
  // The cached module if found and valid, nullptr otherwise. Shared ownership avoids copying
  // potentially multi-MB module binaries on every cache hit.
  std::shared_ptr<const std::string> module;
  // True if a fetch operation is already in progress for this key.
  bool fetch_in_progress;
  // True if a cache entry exists (even if in_progress or expired for negative cache).
  bool cache_hit;
};

/**
 * Callback invoked when async fetch completes.
 * @param module The fetched module bytes, or empty string on failure.
 */
using FetchCallback = std::function<void(const std::string& module)>;

/**
 * Thread-safe cache for remotely fetched dynamic module binaries, keyed by SHA256 hash.
 * Supports positive caching (24h TTL), negative caching (10s TTL), and in-progress tracking.
 */
class DynamicModuleCache : public Logger::Loggable<Logger::Id::config> {
public:
  // Cache TTL in seconds (24 hours).
  static constexpr int CACHE_TTL_SECONDS = 24 * 3600;
  // Negative cache TTL in seconds (10 seconds).
  static constexpr int NEGATIVE_CACHE_SECONDS = 10;
  // In-progress fetch timeout in seconds (5 minutes). Entries stuck in-progress longer than
  // this are treated as failed and evicted, allowing a re-fetch on the next config push.
  static constexpr int IN_PROGRESS_TIMEOUT_SECONDS = 5 * 60;

  DynamicModuleCache() = default;
  ~DynamicModuleCache() = default;

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
   * Updates a cache entry with fetched module.
   * @param key The SHA256 hash key.
   * @param module The fetched module bytes (empty string indicates fetch failure).
   * @param now Current monotonic time.
   */
  void update(const std::string& key, const std::string& module, MonotonicTime now);

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
  absl::flat_hash_map<std::string, ModuleCacheEntry> cache_;
  MonotonicTime::duration time_offset_for_testing_{};
};

/**
 * Singleton accessor for the global module cache.
 */
DynamicModuleCache& getModuleCache();

/**
 * Clears the module cache. Primarily for testing.
 */
void clearModuleCacheForTesting();

/**
 * Sets a time offset for the module cache. Primarily for testing.
 */
void setTimeOffsetForModuleCacheForTesting(MonotonicTime::duration d);

/**
 * Adapter for remote data fetching that integrates with the module cache.
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

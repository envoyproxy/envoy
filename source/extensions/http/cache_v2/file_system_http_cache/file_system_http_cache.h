#pragma once

#include <memory>

#include "envoy/extensions/http/cache_v2/file_system_http_cache/v3/file_system_http_cache.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/filters/http/cache_v2/http_cache.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/stats.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace FileSystemHttpCache {

using ConfigProto =
    envoy::extensions::http::cache_v2::file_system_http_cache::v3::FileSystemHttpCacheV2Config;

class CacheEvictionThread;
struct CacheShared;

/**
 * An instance of a cache. There may be multiple caches in a single envoy configuration.
 * Caches are jointly owned by filters using the cache and the filter configurations.
 * When the filter configurations are destroyed and all cache actions from those filters
 * are resolved, the cache instance is destroyed.
 * Cache instances jointly own the CacheSingleton.
 * If all cache instances are destroyed, the CacheSingleton is destroyed.
 *
 * See DESIGN.md for details of cache behavior.
 */
class FileSystemHttpCache : public HttpCache,
                            public std::enable_shared_from_this<FileSystemHttpCache>,
                            public Logger::Loggable<Logger::Id::cache_filter> {
public:
  FileSystemHttpCache(Singleton::InstanceSharedPtr owner,
                      CacheEvictionThread& cache_eviction_thread, ConfigProto config,
                      std::shared_ptr<Common::AsyncFiles::AsyncFileManager>&& async_file_manager,
                      Stats::Scope& stats_scope);
  ~FileSystemHttpCache() override;

  // Overrides for HttpCache
  CacheInfo cacheInfo() const override;
  void lookup(LookupRequest&& lookup, LookupCallback&& callback) override;
  void evict(Event::Dispatcher& dispatcher, const Key& key) override;
  void touch(const Key& key, SystemTime timestamp) override;
  void updateHeaders(Event::Dispatcher& dispatcher, const Key& key,
                     const Http::ResponseHeaderMap& updated_headers,
                     const ResponseMetadata& updated_metadata) override;
  void insert(Event::Dispatcher& dispatcher, Key key, Http::ResponseHeaderMapPtr headers,
              ResponseMetadata metadata, HttpSourcePtr source,
              std::shared_ptr<CacheProgressReceiver> progress) override;

  const CacheStats& stats() const;

  /**
   * The config of this cache. Used by the factory to ensure there aren't incompatible
   * configs using the same path.
   * @return the config of this cache.
   */
  const ConfigProto& config() const;

  /**
   * Returns the extension name.
   * @return the extension name.
   */
  static absl::string_view name();

  /**
   * Returns a filename for the cache entry with the given key.
   * @param key the key for which to generate a filename.
   * @return a filename for that cache entry (path not included).
   */
  std::string generateFilename(const Key& key) const;

  /**
   * Returns the path for this cache instance. Guaranteed to end in a path-separator.
   * @return the configured path for this cache instance.
   */
  absl::string_view cachePath() const;

  /**
   * Updates stats to reflect that a file has been added to the cache.
   * @param file_size The size in bytes of the file that was added.
   */
  void trackFileAdded(uint64_t file_size);

  /**
   * Updates stats to reflect that a file has been removed from the cache.
   * @param file_size The size in bytes of the file that was removed.
   */
  void trackFileRemoved(uint64_t file_size);

  // Waits for all queued actions to be completed.
  inline void drainAsyncFileActionsForTest() { async_file_manager_->waitForIdle(); };

private:
  // A shared_ptr to keep the cache singleton alive as long as any of its caches are in use.
  const Singleton::InstanceSharedPtr owner_;

  std::shared_ptr<Common::AsyncFiles::AsyncFileManager> async_file_manager_;

  // Stats and config are held in a shared_ptr so that CacheEvictionThread can use
  // them even if the cache instance has been deleted while it performed work.
  std::shared_ptr<CacheShared> shared_;

  // This reference must be declared after owner_, since it can potentially be
  // invalid after owner_ is destroyed.
  CacheEvictionThread& cache_eviction_thread_;

  // Allow test access to cache_eviction_thread_ for synchronization.
  friend class FileSystemCacheTestContext;

  CacheInfo cache_info_;
};

// This part of the cache implementation is shared between CacheEvictionThread and
// FileSystemHttpCache. The implementation of CacheShared is also split between the
// two implementation files, accordingly.
struct CacheShared {
  CacheShared(ConfigProto config, Stats::Scope& stats_scope, CacheEvictionThread& eviction_thread);
  absl::Mutex signal_mu_;
  std::function<void()> signal_eviction_ ABSL_GUARDED_BY(signal_mu_);
  const ConfigProto config_;
  CacheStatNames stat_names_;
  CacheStats stats_;
  // These are part of stats, but we have to track them separately because there is
  // potential to go "less than zero" due to not having sole control of the file cache;
  // gauge values don't have fine enough control to prevent that, and aren't allowed to
  // be negative.
  //
  // See comment on size_bytes and size_count in stats.h for explanation of how stat
  // values can be out of sync with the actionable cache.
  std::atomic<uint64_t> size_count_ = 0;
  std::atomic<uint64_t> size_bytes_ = 0;
  bool needs_init_ = true;

  /**
   * When the cache is deleted, cache state metrics may still be being updated - the
   * cache eviction thread may or may not outlive that, so updates to cache state
   * must be prevented from triggering eviction beyond that deletion.
   */
  void disconnectEviction();

  /**
   * @return true if the eviction thread should do a pass over this cache.
   */
  bool needsEviction() const;

  /**
   * Returns the path for this cache instance. Guaranteed to end in a path-separator.
   * @return the configured path for this cache instance.
   */
  absl::string_view cachePath() const { return config_.cache_path(); }

  /**
   * Updates stats (size and count) to reflect that a file has been added to the cache.
   * @param file_size The size in bytes of the file that was added.
   */
  void trackFileAdded(uint64_t file_size);

  /**
   * Updates stats (size and count) to reflect that a file has been removed from the cache.
   * @param file_size The size in bytes of the file that was removed.
   */
  void trackFileRemoved(uint64_t file_size);

  /**
   * Performs an eviction pass over this cache. Runs in the CacheEvictionThread.
   */
  void evict();

  /**
   * Initializes the stats for this cache. Runs in the CacheEvictionThread.
   */
  void initStats();
};

} // namespace FileSystemHttpCache
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

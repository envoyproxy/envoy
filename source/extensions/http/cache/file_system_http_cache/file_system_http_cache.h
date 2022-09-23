#pragma once

#include <memory>

#include "envoy/extensions/http/cache/file_system_http_cache/v3/file_system_http_cache.pb.h"

#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_entry.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

using ConfigProto =
    envoy::extensions::http::cache::file_system_http_cache::v3::FileSystemHttpCacheConfig;

class FileSystemHttpCache : public HttpCache,
                            public std::enable_shared_from_this<FileSystemHttpCache>,
                            public Logger::Loggable<Logger::Id::cache_filter> {
public:
  FileSystemHttpCache(Singleton::InstanceSharedPtr owner, ConfigProto config,
                      TimeSource& time_source,
                      std::shared_ptr<Common::AsyncFiles::AsyncFileManager>&& async_file_manager);
  // HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& lookup,
                                     Http::StreamDecoderFilterCallbacks& callbacks) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context,
                                     Http::StreamEncoderFilterCallbacks& callbacks) override;
  void updateHeaders(const LookupContext& lookup_context,
                     const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata) override;
  CacheInfo cacheInfo() const override;

  // For the factory to ensure there aren't incompatible configs on the same path.
  const ConfigProto& config() { return config_; }

  // If the cache entry is already a `CacheEntryWorkInProgress`, returns a nullptr,
  // indicating insert should be cancelled. This prevents us from accidentally
  // performing many parallel writes of the same content.
  //
  // If the cache entry is empty, sets it to a `CacheEntryWorkInProgress`, and returns
  // a `CacheEntryFile` with enough details to create and write the cache file. When
  // the writing is complete, the returned `CacheEntryFile` should be passed back in
  // through `updateEntryToFile`.
  //
  // If the cache entry is already a `CacheEntryFile`, a new `CacheEntryFile` is
  // returned with sufficient details to populate a replacement, and the existing entry
  // is replaced with a `CacheEntryWorkInProgress` that contains a pointer to the
  // previous `CacheEntryFile` so it can still be read while the update is occurring.
  ABSL_MUST_USE_RESULT std::shared_ptr<CacheEntryFile> setCacheEntryWorkInProgress(const Key& key)
      ABSL_LOCKS_EXCLUDED(cache_mu_);

  // Writes a vary cache file in the background, and inserts a `CacheEntryVaryRedirect` at `key`
  // if one is not already present.
  // If the `varied_key` cache entry is unpopulated, populates it with a `CacheEntryWorkInProgress`
  // and returns a new `CacheEntryFile` for that key. Returns a nullptr if the `varied_key` cache
  // entry is already populated.
  ABSL_MUST_USE_RESULT std::shared_ptr<CacheEntryFile>
  setCacheEntryToVary(const Key& key, const Http::ResponseHeaderMap& response_headers,
                      const Key& varied_key) ABSL_LOCKS_EXCLUDED(cache_mu_);

  // Removes the `CacheEntryWorkInProgress` at `key`.
  // It is an error to call this with a `key` that doesn't point at a `CacheEntryWorkInProgress`.
  void removeCacheEntryInProgress(const Key& key) ABSL_LOCKS_EXCLUDED(cache_mu_);

  // Replaces the `CacheEntryWorkInProgress` at `key` with the given `CacheEntryFile`.
  // It is an error to call this with a `key` that doesn't point at a `CacheEntryWorkInProgress`
  // or a `CacheEntryFile` to be replaced.
  void updateEntryToFile(const Key& key, std::shared_ptr<CacheEntryFile>&& file_entry)
      ABSL_LOCKS_EXCLUDED(cache_mu_);

  static absl::string_view name();

  enum class PurgeOption { PurgeFile, KeepFile };
  void removeCacheEntry(const Key& key, std::shared_ptr<CacheEntryFile>&& value,
                        PurgeOption purge_option) ABSL_LOCKS_EXCLUDED(cache_mu_);

  void purgeCacheFile(std::shared_ptr<CacheEntryFile>&& cache_entry);

  std::string generateFilename(const Key& key);
  absl::string_view cachePath() const;

  void populateFromDisk();

private:
  void removeLeastRecentlyUsed();
  void updateHeadersInFile(std::string filename, CacheEntryFile::HeaderData header_data);
  void writeVaryNodeToDisk(const Key& key, std::shared_ptr<CacheEntryVaryRedirect> vary_node);
  ABSL_MUST_USE_RESULT std::shared_ptr<CacheEntryFile>
  setCacheEntryWorkInProgressLocked(const Key& key) ABSL_EXCLUSIVE_LOCKS_REQUIRED(cache_mu_);

  // An incrementing value to prevent hash collisions from causing file conflicts.
  std::atomic<uint32_t> filename_suffix_ = 0;
  // A shared_ptr to keep the cache singleton alive as long as any of its caches are in use.
  const Singleton::InstanceSharedPtr owner_;
  const ConfigProto config_;
  TimeSource& time_source_;

  absl::Mutex cache_mu_;
  absl::flat_hash_map<Key, CacheEntrySharedPtr, MessageUtil, MessageUtil>
      cache_ ABSL_GUARDED_BY(cache_mu_);

  std::shared_ptr<Common::AsyncFiles::AsyncFileManager> async_file_manager_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

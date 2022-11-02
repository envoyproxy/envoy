#pragma once

#include <memory>

#include "envoy/extensions/http/cache/file_system_http_cache/v3/file_system_http_cache.pb.h"

#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/filters/http/cache/http_cache.h"

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
                      std::shared_ptr<Common::AsyncFiles::AsyncFileManager>&& async_file_manager);
  // HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& lookup,
                                     Http::StreamDecoderFilterCallbacks& callbacks) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context,
                                     Http::StreamEncoderFilterCallbacks& callbacks) override;

  // Replaces the headers of a cache entry.
  //
  // To avoid races between readers and writers, this unfortunately must be performed by
  // making a full copy of the original cache entry, and then replacing the original with
  // that new file.
  //
  // For example, if we simply overwrote the headers in place, a reader might be about to
  // read the headers expecting them to have one length, then read them with a different
  // length (or in a partially overwritten state).
  //
  // If we opened the file, unlinked it, modified it, and linked it, a reader might still
  // have already had the original version open and still get the same race as above.
  //
  // Therefore the only way to reliably ensure that there are no readers of the file while
  // it is being written (other than something involving system-wide locks that would have
  // other issues), is to rewrite the contents to a new file. This may be expensive
  // for large cache files.
  void updateHeaders(const LookupContext& lookup_context,
                     const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata,
                     std::function<void(bool)> on_complete) override;
  CacheInfo cacheInfo() const override;

  // For the factory to ensure there aren't incompatible configs on the same path.
  const ConfigProto& config() { return config_; }

  // Returns true if the given key currently has a stream writing to it.
  bool workInProgress(const Key& key);

  // Inserts the key into entries_being_written_, excluding it from being written to by
  // another task until the returned Cleanup is destroyed.
  //
  // If the key is already in entries_being_written_, returns nullptr, indicating that
  // the caller should not continue with the write.
  ABSL_MUST_USE_RESULT std::shared_ptr<Cleanup> maybeStartWritingEntry(const Key& key)
      ABSL_LOCKS_EXCLUDED(cache_mu_);

  // Returns a key of the base key plus vary_identifier, if a vary_identifier can be
  // generated from the inputs. Otherwise returns nullopt.
  static absl::optional<Key>
  makeVaryKey(const Key& base, const VaryAllowList& vary_allow_list,
              const absl::btree_set<absl::string_view>& vary_header_values,
              const Http::RequestHeaderMap& request_headers);

  // Writes a vary cache file in the background, and inserts a `CacheEntryVaryRedirect` at `key`
  // if one is not already present.
  // If the `varied_key` cache entry is unpopulated, populates it with a `CacheEntryWorkInProgress`
  // and returns a new `CacheEntryFile` for that key. Returns a nullptr if the `varied_key` cache
  // entry is already populated.
  ABSL_MUST_USE_RESULT std::shared_ptr<Cleanup>
  setCacheEntryToVary(const Key& key, const Http::ResponseHeaderMap& response_headers,
                      const Key& varied_key, std::shared_ptr<Cleanup> cleanup)
      ABSL_LOCKS_EXCLUDED(cache_mu_);

  static absl::string_view name();

  std::string generateFilename(const Key& key) const;
  absl::string_view cachePath() const;
  std::shared_ptr<Common::AsyncFiles::AsyncFileManager> asyncFileManager() const {
    return async_file_manager_;
  }

private:
  void writeVaryNodeToDisk(const Key& key, const Http::ResponseHeaderMap& response_headers,
                           std::shared_ptr<Cleanup> cleanup);

  // A shared_ptr to keep the cache singleton alive as long as any of its caches are in use.
  const Singleton::InstanceSharedPtr owner_;
  const ConfigProto config_;

  absl::Mutex cache_mu_;
  // When a new cache entry is being written, its key will be here and the cache file
  // will not be present. The cache miss will be detected normally from the filesystem.
  // This should be checked before writing; cancel the write if another thread is already
  // writing the same entry.
  absl::flat_hash_set<Key, MessageUtil, MessageUtil>
      entries_being_written_ ABSL_GUARDED_BY(cache_mu_);

  std::shared_ptr<Common::AsyncFiles::AsyncFileManager> async_file_manager_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

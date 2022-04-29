#pragma once

#include <functional>

#include "envoy/common/time.h"
#include "envoy/http/header_map.h"

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/filters/http/cache/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/filters/http/cache/file_system_http_cache/cache_file_header.pb.h"
#include "source/extensions/filters/http/cache/http_cache.h"

#include "absl/container/btree_set.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

using ::Envoy::Extensions::Common::AsyncFiles::AsyncFileManager;

Buffer::OwnedImpl headersToBuffer(const CacheFileHeader& headers);
Buffer::OwnedImpl trailersToBuffer(const CacheFileTrailer& trailers);

class CacheEntry : public NonCopyable {
public:
  void touch(TimeSource& time_source);
  virtual ~CacheEntry() = default;

protected:
  CacheEntry() = default;

private:
  Envoy::SystemTime last_touched_;
};

class CacheEntryFile;

class CacheEntryWorkInProgress : public CacheEntry {
public:
  static std::shared_ptr<CacheEntryWorkInProgress>
  create(std::shared_ptr<CacheEntryFile> entry_being_replaced);

  std::shared_ptr<CacheEntryFile> entryBeingReplaced() const { return entry_being_replaced_; }

private:
  explicit CacheEntryWorkInProgress(std::shared_ptr<CacheEntryFile>&& entry_being_replaced)
      : entry_being_replaced_(entry_being_replaced) {}
  std::shared_ptr<CacheEntryFile> entry_being_replaced_;
};

class CacheEntryFile : public CacheEntry {
public:
  static std::shared_ptr<CacheEntryFile> create(AsyncFileManager& file_manager,
                                                std::string&& filename);
  AsyncFileManager& fileManager() { return file_manager_; }
  absl::string_view filename() { return filename_; }
  void setHeaderData(const CacheFileFixedBlock& block, CacheFileHeader header_data)
      ABSL_LOCKS_EXCLUDED(header_mu_);
  struct HeaderData {
    CacheFileFixedBlock block;
    CacheFileHeader proto;
  };
  HeaderData getHeaderData() ABSL_LOCKS_EXCLUDED(header_mu_);
  // onDestroy can be set to delete the file when the cache entry is destroyed - this is
  // used to postpone purging of the file itself until all users of the cache entry are
  // done with it.
  void setOnDestroy(std::function<void()> on_destroy) { on_destroy_ = on_destroy; }
  ~CacheEntryFile() override;

private:
  CacheEntryFile(AsyncFileManager& file_manager, std::string&& filename);
  AsyncFileManager& file_manager_;
  std::string filename_;
  // header_data_ is used from memory to avoid races, as it can be updated when the rest of
  // the file remains the same. Header updates from updateHeaders are applied in memory
  // immediately, and also applied to the cache file asynchronously.
  absl::Mutex header_mu_;
  HeaderData header_data_ ABSL_GUARDED_BY(header_mu_);
  std::function<void()> on_destroy_ = []() {};
};

class CacheEntryVaryRedirect : public CacheEntry {
public:
  static std::shared_ptr<CacheEntryVaryRedirect>
  create(const absl::btree_set<absl::string_view>& vary_allow_list);

  // Returns the appropriate key for the specific request with vary-headers.
  //
  // May return no key if vary_allow_list has changed since the cache was populated,
  // or the headers of the request are invalid for the cache.
  absl::optional<Key> varyKey(const Key& base, const VaryAllowList& vary_allow_list,
                              const Http::RequestHeaderMap& request_headers) const;

  // Returns a buffer containing the file contents for this cache entry.
  Buffer::OwnedImpl asBuffer() const;

private:
  explicit CacheEntryVaryRedirect(const absl::btree_set<absl::string_view>& vary_header_values);
  // The values from the Vary response header, comma-separated and sorted.
  std::string vary_header_values_copy_;
  const absl::btree_set<absl::string_view> vary_header_values_;
};

using CacheEntrySharedPtr = std::shared_ptr<CacheEntry>;

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

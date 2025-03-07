#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

class ActiveCacheEntry;
class FileSystemHttpCache;

using Envoy::Extensions::Common::AsyncFiles::AsyncFileHandle;

class FileLookupContext {
public:
  static void begin(Event::Dispatcher& dispatcher, AsyncFileHandle handle,
                    HttpCache::LookupCallback&& callback);

private:
  FileLookupContext(Event::Dispatcher& dispatcher, AsyncFileHandle handle,
                    HttpCache::LookupCallback&& callback);
  void getHeaderBlock();
  void getHeaders();
  void getTrailers();
  void done(absl::StatusOr<LookupResult>&& result);

  Event::Dispatcher& dispatcher_;
  AsyncFileHandle file_handle_;
  CacheFileFixedBlock header_block_;
  HttpCache::LookupCallback callback_;
  LookupResult result_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

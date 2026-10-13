#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/filters/http/cache_v2/http_cache.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_fixed_block.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace FileSystemHttpCache {

class CacheSession;
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
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

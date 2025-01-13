#pragma once

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

class CacheFileReader : public CacheReader {
public:
  CacheFileReader(Common::AsyncFiles::AsyncFileHandle handle);
  ~CacheFileReader() override;
  // From CacheReader
  void getBody(Event::Dispatcher& dispatcher, AdjustedByteRange range, GetBodyCallback&& cb) final;

private:
  Common::AsyncFiles::AsyncFileHandle file_handle_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

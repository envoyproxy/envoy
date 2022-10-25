#include "source/extensions/http/cache/file_system_http_cache/cache_entry_file.h"

#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

CacheEntryFile::CacheEntryFile(FileSystemHttpCache& cache, Key key,
                               std::shared_ptr<Cleanup> cleanup)
    : key_(key), filename_(cache.generateFilename(key)), cache_path_(cache.cachePath()),
      async_file_manager_(cache.asyncFileManager()), cleanup_(std::move(cleanup)) {}

std::unique_ptr<CacheEntryFile> CacheEntryFile::create(FileSystemHttpCache& cache, Key key,
                                                       std::shared_ptr<Cleanup> cleanup) {
  return std::make_unique<CacheEntryFile>(cache, key, std::move(cleanup));
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

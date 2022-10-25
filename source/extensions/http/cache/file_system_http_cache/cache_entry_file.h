#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

class FileSystemHttpCache;

class CacheEntryFile {
public:
  CacheEntryFile(FileSystemHttpCache& cache, Key key, std::shared_ptr<Cleanup> cleanup);
  static std::unique_ptr<CacheEntryFile> create(FileSystemHttpCache& cache, Key key,
                                                std::shared_ptr<Cleanup> cleanup);

  Common::AsyncFiles::AsyncFileManager& asyncFileManager() { return *async_file_manager_; }
  absl::string_view cachePath() { return cache_path_; }
  absl::string_view filename() { return filename_; }
  Key& key() { return key_; }

private:
  Key key_;
  std::string filename_;
  std::string cache_path_;
  std::shared_ptr<Common::AsyncFiles::AsyncFileManager> async_file_manager_;
  std::shared_ptr<Cleanup> cleanup_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

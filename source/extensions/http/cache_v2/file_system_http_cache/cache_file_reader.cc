#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_reader.h"

#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_fixed_block.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace FileSystemHttpCache {

using Common::AsyncFiles::AsyncFileHandle;

CacheFileReader::CacheFileReader(AsyncFileHandle handle) : file_handle_(handle) {}

void CacheFileReader::getBody(Event::Dispatcher& dispatcher, AdjustedByteRange range,
                              GetBodyCallback&& cb) {
  auto queued = file_handle_->read(
      &dispatcher, CacheFileFixedBlock::offsetToBody() + range.begin(), range.length(),
      [len = range.length(),
       cb = std::move(cb)](absl::StatusOr<Buffer::InstancePtr> read_result) mutable -> void {
        if (!read_result.ok()) {
          return cb(nullptr, EndStream::Reset);
        }
        if (read_result.value()->length() != len) {
          return cb(nullptr, EndStream::Reset);
        }
        return cb(std::move(read_result.value()), EndStream::More);
      });
  ASSERT(queued.ok(), queued.status().ToString());
}

CacheFileReader::~CacheFileReader() {
  auto queued = file_handle_->close(nullptr, [](absl::Status) {});
  ASSERT(queued.ok());
}

} // namespace FileSystemHttpCache
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

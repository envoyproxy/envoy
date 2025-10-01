#include "source/extensions/http/cache_v2/file_system_http_cache/lookup_context.h"

#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_header.pb.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_reader.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace FileSystemHttpCache {

FileLookupContext::FileLookupContext(Event::Dispatcher& dispatcher, AsyncFileHandle handle,
                                     HttpCache::LookupCallback&& callback)
    : dispatcher_(dispatcher), file_handle_(std::move(handle)), callback_(std::move(callback)) {}

void FileLookupContext::begin(Event::Dispatcher& dispatcher, AsyncFileHandle handle,
                              HttpCache::LookupCallback&& callback) {
  // bare pointer because this object owns itself - it gets captured in
  // lambdas and is deleted when 'done' is eventually called.
  FileLookupContext* p = new FileLookupContext(dispatcher, std::move(handle), std::move(callback));
  p->getHeaderBlock();
}

void FileLookupContext::done(absl::StatusOr<LookupResult>&& result) {
  if (!result.ok() || result.value().cache_reader_ == nullptr) {
    auto queued = file_handle_->close(nullptr, [](absl::Status) {});
    ASSERT(queued.ok(), queued.status().ToString());
  }
  auto cb = std::move(callback_);
  delete this;
  cb(std::move(result));
}

absl::Status cacheEntryInvalidStatus() { return absl::DataLossError("corrupted cache file"); }

void FileLookupContext::getHeaderBlock() {
  auto queued =
      file_handle_->read(&dispatcher_, 0, CacheFileFixedBlock::size(),
                         [this](absl::StatusOr<Buffer::InstancePtr> read_result) -> void {
                           if (!read_result.ok()) {
                             return done(read_result.status());
                           }
                           if (read_result.value()->length() != CacheFileFixedBlock::size()) {
                             return done(cacheEntryInvalidStatus());
                           }
                           header_block_.populateFromStringView(read_result.value()->toString());
                           if (!header_block_.isValid()) {
                             return done(cacheEntryInvalidStatus());
                           }
                           if (header_block_.trailerSize()) {
                             getTrailers();
                           } else {
                             getHeaders();
                           }
                         });
  ASSERT(queued.ok(), queued.status().ToString());
}

void FileLookupContext::getHeaders() {
  auto queued =
      file_handle_->read(&dispatcher_, header_block_.offsetToHeaders(), header_block_.headerSize(),
                         [this](absl::StatusOr<Buffer::InstancePtr> read_result) -> void {
                           if (!read_result.ok()) {
                             return done(read_result.status());
                           }
                           if (read_result.value()->length() != header_block_.headerSize()) {
                             return done(cacheEntryInvalidStatus());
                           }
                           auto header_proto = makeCacheFileHeaderProto(*read_result.value());
                           result_.response_headers_ = headersFromHeaderProto(header_proto);
                           result_.response_metadata_ = metadataFromHeaderProto(header_proto);
                           result_.body_length_ = header_block_.bodySize();
                           result_.cache_reader_ =
                               std::make_unique<CacheFileReader>(std::move(file_handle_));
                           return done(std::move(result_));
                         });
  ASSERT(queued.ok(), queued.status().ToString());
}

void FileLookupContext::getTrailers() {
  auto queued = file_handle_->read(
      &dispatcher_, header_block_.offsetToTrailers(), header_block_.trailerSize(),
      [this](absl::StatusOr<Buffer::InstancePtr> read_result) -> void {
        if (!read_result.ok()) {
          return done(read_result.status());
        }
        if (read_result.value()->length() != header_block_.trailerSize()) {
          return done(cacheEntryInvalidStatus());
        }
        auto trailer_proto = makeCacheFileTrailerProto(*read_result.value());
        result_.response_trailers_ = trailersFromTrailerProto(trailer_proto);
        getHeaders();
      });
  ASSERT(queued.ok(), queued.status().ToString());
}

} // namespace FileSystemHttpCache
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/http/cache/file_system_http_cache/lookup_context.h"

#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header.pb.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

std::string FileLookupContext::filepath() {
  return absl::StrCat(cache_.cachePath(), cache_.generateFilename(key_));
}

bool FileLookupContext::workInProgress() const { return cache_.workInProgress(key()); }

void FileLookupContext::getHeaders(LookupHeadersCallback&& cb) {
  lookup_headers_callback_ = std::move(cb);
  tryOpenCacheFile();
}

void FileLookupContext::tryOpenCacheFile() {
  cancel_action_in_flight_ = cache_.asyncFileManager()->openExistingFile(
      dispatcher(), filepath(), Common::AsyncFiles::AsyncFileManager::Mode::ReadOnly,
      [this](absl::StatusOr<AsyncFileHandle> open_result) {
        cancel_action_in_flight_ = nullptr;
        if (!open_result.ok()) {
          return doCacheMiss();
        }
        ASSERT(!file_handle_);
        file_handle_ = std::move(open_result.value());
        getHeaderBlockFromFile();
      });
}

void FileLookupContext::doCacheMiss() {
  cache_.stats().cache_miss_.inc();
  std::move(lookup_headers_callback_)(LookupResult{}, /* end_stream (ignored) = */ false);
  lookup_headers_callback_ = nullptr;
}

void FileLookupContext::doCacheEntryInvalid() {
  invalidateCacheEntry();
  doCacheMiss();
}

void FileLookupContext::getHeaderBlockFromFile() {
  ASSERT(dispatcher()->isThreadSafe());
  auto queued = file_handle_->read(
      dispatcher(), 0, CacheFileFixedBlock::size(),
      [this](absl::StatusOr<Buffer::InstancePtr> read_result) {
        ASSERT(dispatcher()->isThreadSafe());
        cancel_action_in_flight_ = nullptr;
        if (!read_result.ok() || read_result.value()->length() != CacheFileFixedBlock::size()) {
          return doCacheEntryInvalid();
        }
        header_block_.populateFromStringView(read_result.value()->toString());
        if (!header_block_.isValid()) {
          return doCacheEntryInvalid();
        }
        getHeadersFromFile();
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileLookupContext::getHeadersFromFile() {
  ASSERT(dispatcher()->isThreadSafe());
  auto queued = file_handle_->read(
      dispatcher(), header_block_.offsetToHeaders(), header_block_.headerSize(),
      [this](absl::StatusOr<Buffer::InstancePtr> read_result) {
        ASSERT(dispatcher()->isThreadSafe());
        cancel_action_in_flight_ = nullptr;
        if (!read_result.ok() || read_result.value()->length() != header_block_.headerSize()) {
          return doCacheEntryInvalid();
        }
        auto header_proto = makeCacheFileHeaderProto(*read_result.value());
        if (header_proto.headers_size() == 1 && header_proto.headers().at(0).key() == "vary") {
          auto maybe_vary_key = cache_.makeVaryKey(
              key_, lookup().varyAllowList(),
              absl::StrSplit(header_proto.headers().at(0).value(), ','), lookup().requestHeaders());
          if (!maybe_vary_key.has_value()) {
            return doCacheMiss();
          }
          key_ = maybe_vary_key.value();
          return closeFileAndGetHeadersAgainWithNewVaryKey();
        }
        cache_.stats().cache_hit_.inc();
        std::move(lookup_headers_callback_)(
            lookup().makeLookupResult(headersFromHeaderProto(header_proto),
                                      metadataFromHeaderProto(header_proto),
                                      header_block_.bodySize()),
            /* end_stream = */ header_block_.trailerSize() == 0 && header_block_.bodySize() == 0);
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileLookupContext::closeFileAndGetHeadersAgainWithNewVaryKey() {
  ASSERT(dispatcher()->isThreadSafe());
  auto queued = file_handle_->close(dispatcher(), [this](absl::Status) {
    ASSERT(dispatcher()->isThreadSafe());
    file_handle_ = nullptr;
    // Restart with the new key.
    return tryOpenCacheFile();
  });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileLookupContext::invalidateCacheEntry() {
  ASSERT(dispatcher()->isThreadSafe());
  // We don't capture the cancel action here because we want these operations to continue even
  // if the filter was destroyed in the meantime. For the same reason, we must not capture 'this'.
  cache_.asyncFileManager()->stat(
      dispatcher(), filepath(),
      [file = filepath(), cache = cache_.shared_from_this(),
       dispatcher = dispatcher()](absl::StatusOr<struct stat> stat_result) {
        ASSERT(dispatcher->isThreadSafe());
        size_t file_size = 0;
        if (stat_result.ok()) {
          file_size = stat_result.value().st_size;
        }
        cache->asyncFileManager()->unlink(dispatcher, file,
                                          [cache, file_size](absl::Status unlink_result) {
                                            if (unlink_result.ok()) {
                                              cache->trackFileRemoved(file_size);
                                            }
                                          });
      });
}

void FileLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) {
  ASSERT(dispatcher()->isThreadSafe());
  ASSERT(cb);
  ASSERT(!cancel_action_in_flight_);
  ASSERT(file_handle_);
  auto queued = file_handle_->read(
      dispatcher(), header_block_.offsetToBody() + range.begin(), range.length(),
      [this, cb = std::move(cb), range](absl::StatusOr<Buffer::InstancePtr> read_result) mutable {
        ASSERT(dispatcher()->isThreadSafe());
        cancel_action_in_flight_ = nullptr;
        if (!read_result.ok() || read_result.value()->length() != range.length()) {
          invalidateCacheEntry();
          // Calling callback with nullptr fails the request.
          std::move(cb)(nullptr, /* end_stream (ignored) = */ false);
          return;
        }
        std::move(cb)(std::move(read_result.value()),
                      /* end_stream = */ range.end() == header_block_.bodySize() &&
                          header_block_.trailerSize() == 0);
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileLookupContext::getTrailers(LookupTrailersCallback&& cb) {
  ASSERT(dispatcher()->isThreadSafe());
  ASSERT(cb);
  ASSERT(!cancel_action_in_flight_);
  ASSERT(file_handle_);
  auto queued = file_handle_->read(
      dispatcher(), header_block_.offsetToTrailers(), header_block_.trailerSize(),
      [this, cb = std::move(cb)](absl::StatusOr<Buffer::InstancePtr> read_result) mutable {
        ASSERT(dispatcher()->isThreadSafe());
        cancel_action_in_flight_ = nullptr;
        if (!read_result.ok() || read_result.value()->length() != header_block_.trailerSize()) {
          invalidateCacheEntry();
          // There is no failure response for getTrailers, so we just
          // say there were no trailers in the event of this failure.
          std::move(cb)(Http::ResponseTrailerMapImpl::create());
          return;
        }
        CacheFileTrailer trailer;
        trailer.ParseFromString(read_result.value()->toString());
        std::move(cb)(trailersFromTrailerProto(trailer));
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = std::move(queued.value());
}

void FileLookupContext::onDestroy() {
  if (cancel_action_in_flight_) {
    std::move(cancel_action_in_flight_)();
    cancel_action_in_flight_ = nullptr;
  }
  if (file_handle_) {
    auto status = file_handle_->close(nullptr, [](absl::Status) {});
    ASSERT(status.ok(), status.status().ToString());
    file_handle_ = nullptr;
  }
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

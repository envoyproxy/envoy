#include "source/extensions/http/cache/file_system_http_cache/lookup_context.h"

#include "source/extensions/http/cache/file_system_http_cache/cache_file_header.pb.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

void NoOpLookupContext::getHeaders(LookupHeadersCallback&& cb) { cb(LookupResult{}); }

void NoOpLookupContext::getBody(const AdjustedByteRange&, LookupBodyCallback&&) {
  IS_ENVOY_BUG("NoOpLookupContext should never act beyond getHeaders");
}

void NoOpLookupContext::getTrailers(LookupTrailersCallback&&) {
  IS_ENVOY_BUG("NoOpLookupContext should never act beyond getHeaders");
}

void FileLookupContext::getHeaders(LookupHeadersCallback&& cb) {
  auto header_data = cache_entry_->getHeaderData();
  cb(lookup().makeLookupResult(headersFromHeaderProto(header_data.proto),
                               metadataFromHeaderProto(header_data.proto),
                               header_data.block.bodySize(), header_data.block.trailerSize() > 0));
}

void FileLookupContext::invalidateCacheEntry() {
  cache_.removeCacheEntry(lookup().key(), std::move(cache_entry_),
                          FileSystemHttpCache::PurgeOption::PurgeFile);
}

void FileLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) {
  absl::MutexLock lock(&mu_);
  ASSERT(!action_in_flight_);
  auto read_chunk = [this, cb, range]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    action_in_flight_ =
        file_handle_
            ->read(CacheFileFixedBlock::offsetToBody() + range.begin(), range.length(),
                   [this, cb, range](absl::StatusOr<Buffer::InstancePtr> read_result) {
                     {
                       absl::MutexLock lock(&mu_);
                       action_in_flight_ = nullptr;
                     }
                     if (!read_result.ok() || read_result.value()->length() != range.length()) {
                       invalidateCacheEntry();
                       // Calling callback with nullptr fails the request.
                       cb(nullptr);
                       return;
                     }
                     cb(std::move(read_result.value()));
                   })
            .value();
  };
  if (file_handle_) {
    read_chunk();
  } else {
    action_in_flight_ = cache_entry_->fileManager().openExistingFile(
        absl::StrCat(cache_.cachePath(), cache_entry_->filename()),
        AsyncFileManager::Mode::ReadOnly,
        [this, cb, read_chunk](absl::StatusOr<AsyncFileHandle> open_result) {
          if (!open_result.ok()) {
            invalidateCacheEntry();
            // Calling callback with nullptr fails the request.
            cb(nullptr);
            return;
          }
          absl::MutexLock lock(&mu_);
          file_handle_ = open_result.value();
          read_chunk();
        });
  }
}

void FileLookupContext::getTrailers(LookupTrailersCallback&& cb) {
  ASSERT(cb);
  absl::MutexLock lock(&mu_);
  ASSERT(!action_in_flight_);
  auto header_data = cache_entry_->getHeaderData();
  auto sz = header_data.block.trailerSize();
  action_in_flight_ = file_handle_
                          ->read(header_data.block.offsetToTrailers(), sz,
                                 [this, cb, sz](absl::StatusOr<Buffer::InstancePtr> read_result) {
                                   {
                                     absl::MutexLock lock(&mu_);
                                     action_in_flight_ = nullptr;
                                   }
                                   if (!read_result.ok() || read_result.value()->length() != sz) {
                                     invalidateCacheEntry();
                                     // There is no failure response for getTrailers, so we just say
                                     // there were no trailers in the event of this failure.
                                     cb(Http::ResponseTrailerMapImpl::create());
                                     return;
                                   }
                                   CacheFileTrailer trailer;
                                   trailer.ParseFromString(read_result.value()->toString());
                                   cb(trailersFromTrailerProto(trailer));
                                 })
                          .value();
}

void FileLookupContext::onDestroy() {
  CancelFunction cancel;
  AsyncFileHandle handle;
  {
    absl::MutexLock lock(&mu_);
    cancel = std::move(action_in_flight_);
    handle = file_handle_;
    file_handle_ = nullptr;
  }
  while (cancel) {
    // We mustn't hold the lock while calling cancel, as it can potentially wait for
    // a callback to complete, and the callback might take the lock.
    cancel();
    {
      // It's possible that while calling cancel, another action was started - if
      // that happened, we must cancel that one too!
      absl::MutexLock lock(&mu_);
      cancel = std::move(action_in_flight_);
      handle = file_handle_;
      file_handle_ = nullptr;
    }
  }
  if (handle) {
    auto status = handle->close([](absl::Status) {});
    ASSERT(status.ok(), status.ToString());
  }
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
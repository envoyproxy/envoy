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
  // TODO(ravenblack): Consider adding a memory cache check here for uncacheable keys, to save
  // on the repeated filesystem hit. Capture some performance metrics to see if it's worth it.
  // If migrating to "shared stream" implementation, this question answers itself.
  absl::MutexLock lock(&mu_);
  getHeadersWithLock(std::move(cb));
}

void FileLookupContext::getHeadersWithLock(LookupHeadersCallback cb) {
  mu_.AssertHeld();
  cancel_action_in_flight_ = cache_.asyncFileManager()->openExistingFile(
      filepath(), Common::AsyncFiles::AsyncFileManager::Mode::ReadOnly,
      [this, cb](absl::StatusOr<AsyncFileHandle> open_result) {
        absl::MutexLock lock(&mu_);
        cancel_action_in_flight_ = nullptr;
        if (!open_result.ok()) {
          cache_.stats().cache_miss_.inc();
          cb(LookupResult{});
          return;
        }
        ASSERT(!file_handle_);
        file_handle_ = std::move(open_result.value());
        auto queued = file_handle_->read(
            0, CacheFileFixedBlock::size(),
            [this, cb](absl::StatusOr<Buffer::InstancePtr> read_result) {
              absl::MutexLock lock(&mu_);
              cancel_action_in_flight_ = nullptr;
              if (!read_result.ok() ||
                  read_result.value()->length() != CacheFileFixedBlock::size()) {
                invalidateCacheEntry();
                cache_.stats().cache_miss_.inc();
                cb(LookupResult{});
                return;
              }
              header_block_.populateFromStringView(read_result.value()->toString());
              if (!header_block_.isValid()) {
                invalidateCacheEntry();
                cache_.stats().cache_miss_.inc();
                cb(LookupResult{});
                return;
              }
              auto queued = file_handle_->read(
                  header_block_.offsetToHeaders(), header_block_.headerSize(),
                  [this, cb](absl::StatusOr<Buffer::InstancePtr> read_result) {
                    absl::MutexLock lock(&mu_);
                    cancel_action_in_flight_ = nullptr;
                    if (!read_result.ok() ||
                        read_result.value()->length() != header_block_.headerSize()) {
                      invalidateCacheEntry();
                      cache_.stats().cache_miss_.inc();
                      cb(LookupResult{});
                      return;
                    }
                    auto header_proto = makeCacheFileHeaderProto(*read_result.value());
                    if (header_proto.headers_size() == 1 &&
                        header_proto.headers().at(0).key() == "vary") {
                      auto maybe_vary_key = cache_.makeVaryKey(
                          key_, lookup().varyAllowList(),
                          absl::StrSplit(header_proto.headers().at(0).value(), ','),
                          lookup().requestHeaders());
                      if (!maybe_vary_key.has_value()) {
                        cache_.stats().cache_miss_.inc();
                        cb(LookupResult{});
                        return;
                      }
                      key_ = maybe_vary_key.value();
                      auto fh = std::move(file_handle_);
                      file_handle_ = nullptr;
                      // It should be possible to cancel close, to make this safe.
                      // (it should still close the file, but cancel the callback.)
                      auto queued = fh->close([this, cb](absl::Status) {
                        absl::MutexLock lock(&mu_);
                        // Restart getHeaders with the new key.
                        return getHeadersWithLock(cb);
                      });
                      ASSERT(queued.ok(), queued.ToString());
                      return;
                    }
                    cache_.stats().cache_hit_.inc();
                    cb(lookup().makeLookupResult(
                        headersFromHeaderProto(header_proto), metadataFromHeaderProto(header_proto),
                        header_block_.bodySize(), header_block_.trailerSize() > 0));
                  });
              ASSERT(queued.ok(), queued.status().ToString());
              cancel_action_in_flight_ = queued.value();
            });
        ASSERT(queued.ok(), queued.status().ToString());
        cancel_action_in_flight_ = queued.value();
      });
}

void FileLookupContext::invalidateCacheEntry() {
  cache_.asyncFileManager()->stat(
      filepath(), [file = filepath(),
                   cache = cache_.shared_from_this()](absl::StatusOr<struct stat> stat_result) {
        size_t file_size = 0;
        if (stat_result.ok()) {
          file_size = stat_result.value().st_size;
        }
        cache->asyncFileManager()->unlink(file, [cache, file_size](absl::Status unlink_result) {
          if (unlink_result.ok()) {
            cache->trackFileRemoved(file_size);
          }
        });
      });
}

void FileLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) {
  absl::MutexLock lock(&mu_);
  ASSERT(!cancel_action_in_flight_);
  auto queued = file_handle_->read(
      header_block_.offsetToBody() + range.begin(), range.length(),
      [this, cb, range](absl::StatusOr<Buffer::InstancePtr> read_result) {
        absl::MutexLock lock(&mu_);
        cancel_action_in_flight_ = nullptr;
        if (!read_result.ok() || read_result.value()->length() != range.length()) {
          invalidateCacheEntry();
          // Calling callback with nullptr fails the request.
          cb(nullptr);
          return;
        }
        cb(std::move(read_result.value()));
      });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = queued.value();
}

void FileLookupContext::getTrailers(LookupTrailersCallback&& cb) {
  ASSERT(cb);
  absl::MutexLock lock(&mu_);
  ASSERT(!cancel_action_in_flight_);
  auto queued = file_handle_->read(header_block_.offsetToTrailers(), header_block_.trailerSize(),
                                   [this, cb](absl::StatusOr<Buffer::InstancePtr> read_result) {
                                     absl::MutexLock lock(&mu_);
                                     cancel_action_in_flight_ = nullptr;
                                     if (!read_result.ok() || read_result.value()->length() !=
                                                                  header_block_.trailerSize()) {
                                       invalidateCacheEntry();
                                       // There is no failure response for getTrailers, so we just
                                       // say there were no trailers in the event of this failure.
                                       cb(Http::ResponseTrailerMapImpl::create());
                                       return;
                                     }
                                     CacheFileTrailer trailer;
                                     trailer.ParseFromString(read_result.value()->toString());
                                     cb(trailersFromTrailerProto(trailer));
                                   });
  ASSERT(queued.ok(), queued.status().ToString());
  cancel_action_in_flight_ = queued.value();
}

void FileLookupContext::onDestroy() {
  CancelFunction cancel;
  {
    absl::MutexLock lock(&mu_);
    cancel = std::move(cancel_action_in_flight_);
    cancel_action_in_flight_ = nullptr;
  }
  while (cancel) {
    // We mustn't hold the lock while calling cancel, as it can potentially wait for
    // a callback to complete, and the callback might take the lock.
    cancel();
    {
      // It's possible that while calling cancel, another action was started - if
      // that happened, we must cancel that one too!
      absl::MutexLock lock(&mu_);
      cancel = std::move(cancel_action_in_flight_);
      cancel_action_in_flight_ = nullptr;
    }
  }
  {
    absl::MutexLock lock(&mu_);
    if (file_handle_) {
      auto status = file_handle_->close([](absl::Status) {});
      ASSERT(status.ok(), status.ToString());
      file_handle_ = nullptr;
    }
  }
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

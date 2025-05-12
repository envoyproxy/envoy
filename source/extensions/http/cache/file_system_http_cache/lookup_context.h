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

class FileSystemHttpCache;

using Envoy::Extensions::Common::AsyncFiles::AsyncFileHandle;
using Envoy::Extensions::Common::AsyncFiles::CancelFunction;

class FileLookupContext : public LookupContext {
public:
  FileLookupContext(Event::Dispatcher& dispatcher, FileSystemHttpCache& cache,
                    LookupRequest&& lookup)
      : dispatcher_(dispatcher), cache_(cache), key_(lookup.key()), lookup_(std::move(lookup)) {}

  // From LookupContext
  void getHeaders(LookupHeadersCallback&& cb) final;
  void getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) final;
  void getTrailers(LookupTrailersCallback&& cb) final;
  void onDestroy() final;
  // This shouldn't be necessary since onDestroy is supposed to always be called, but in some
  // tests it is not.
  ~FileLookupContext() override { onDestroy(); }

  const LookupRequest& lookup() const { return lookup_; }
  const Key& key() const { return key_; }
  bool workInProgress() const;
  Event::Dispatcher* dispatcher() const { return &dispatcher_; }

private:
  void tryOpenCacheFile();
  void doCacheMiss();
  void doCacheEntryInvalid();
  void getHeaderBlockFromFile();
  void getHeadersFromFile();
  void closeFileAndGetHeadersAgainWithNewVaryKey();

  // In the event that the cache failed to retrieve, remove the cache entry from the
  // cache so we don't keep repeating the same failure.
  void invalidateCacheEntry();

  std::string filepath();

  Event::Dispatcher& dispatcher_;

  // We can safely use a reference here, because the shared_ptr to a cache is guaranteed to outlive
  // all filters that use it.
  FileSystemHttpCache& cache_;

  AsyncFileHandle file_handle_;
  CancelFunction cancel_action_in_flight_;
  CacheFileFixedBlock header_block_;
  Key key_;

  LookupHeadersCallback lookup_headers_callback_;
  const LookupRequest lookup_;
};

// TODO(ravenblack): A CacheEntryInProgressReader should be implemented to prevent
// "thundering herd" problem.
//
// First the insert needs to be performed not by using the existing request but by
// issuing its own request[s], otherwise the first client to request a resource could
// provoke failure for any other clients sharing that data-stream, by closing its
// request before the cache population is completed.
//
// The plan is to make the entire cache insert happen "out of band", and to populate
// the cache with a CacheEntryInProgress object, allowing clients to stream from it in
// parallel.
//
// This may require intercepting at the initialization of LookupContext to trigger
// immediate "InProgress" cache insertion for any resource compatible with cache
// insertion, and the beginning of that out-of-band download - this way the original
// requester can be a sibling of any subsequent requester, whereas if we waited for
// the cache filter's insert path to be reached then the process would potentially be
// much more confusing (because we will never want a stream to be doing the inserting
// if we have an external task for that, and because there would be a race where two
// clients could get past the lookup before either creates an InsertContext).
//
// The current, early implementation simply allows requests to bypass the cache when
// the cache entry is in the process of being populated. It is therefore subject to
// the "thundering herd" problem.

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

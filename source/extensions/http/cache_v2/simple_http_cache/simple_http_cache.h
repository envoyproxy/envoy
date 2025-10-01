#pragma once

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/cache_v2/http_cache.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

// Example cache backend that never evicts. Not suitable for production use.
class SimpleHttpCache : public HttpCache {
public:
  class Entry {
  public:
    Entry(Http::ResponseHeaderMapPtr response_headers, ResponseMetadata metadata)
        : response_headers_(std::move(response_headers)), metadata_(std::move(metadata)) {}
    Buffer::InstancePtr body(AdjustedByteRange range) const;
    void appendBody(Buffer::InstancePtr buf);
    uint64_t bodySize() const;
    Http::ResponseHeaderMapPtr copyHeaders() const;
    Http::ResponseTrailerMapPtr copyTrailers() const;
    ResponseMetadata metadata() const;
    void updateHeadersAndMetadata(Http::ResponseHeaderMapPtr response_headers,
                                  ResponseMetadata metadata);
    void setTrailers(Http::ResponseTrailerMapPtr trailers);
    void setEndStreamAfterBody();

  private:
    mutable absl::Mutex mu_;
    // Body can be being written to while being read from, so mutex guarded.
    std::string body_ ABSL_GUARDED_BY(mu_);
    Http::ResponseHeaderMapPtr response_headers_ ABSL_GUARDED_BY(mu_);
    ResponseMetadata metadata_ ABSL_GUARDED_BY(mu_);
    bool end_stream_after_body_{false};
    Http::ResponseTrailerMapPtr trailers_;
  };

  // HttpCache
  CacheInfo cacheInfo() const override;
  void lookup(LookupRequest&& request, LookupCallback&& callback) override;
  void evict(Event::Dispatcher& dispatcher, const Key& key) override;
  // Touch is to influence expiry, this implementation has no expiry.
  void touch(const Key&, SystemTime) override {}
  void updateHeaders(Event::Dispatcher& dispatcher, const Key& key,
                     const Http::ResponseHeaderMap& updated_headers,
                     const ResponseMetadata& updated_metadata) override;
  void insert(Event::Dispatcher& dispatcher, Key key, Http::ResponseHeaderMapPtr headers,
              ResponseMetadata metadata, HttpSourcePtr source,
              std::shared_ptr<CacheProgressReceiver> progress) override;

  absl::Mutex mu_;
  absl::flat_hash_map<Key, std::shared_ptr<Entry>, MessageUtil, MessageUtil>
      entries_ ABSL_GUARDED_BY(mu_);
};

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

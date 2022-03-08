#pragma once

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/cache/http_cache.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "simple_lru_cache/simple_lru_cache_inl.h"

// included to make code_format happy
#include "envoy/extensions/cache/simple_http_cache/v3/config.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// The default maximum size of the cache is 100MiB
constexpr int64_t kSimpleHttpCacheDefaultSize = 100 * 1024 * 1024;

class SimpleHttpCache : public HttpCache {
private:
  struct Entry {
    Http::ResponseHeaderMapPtr response_headers_;
    ResponseMetadata metadata_;
    std::string body_;
    Http::ResponseTrailerMapPtr trailers_;

    // @return int64_t calculates an approximated size (in bytes) by only
    // measuring the bytes in the {header, body, trailers}. This does not account
    // for data structures.
    int64_t byteSize() const {
      int64_t headers_size = response_headers_ != nullptr ? response_headers_->byteSize() : 0;
      int64_t trailers_size = trailers_ != nullptr ? trailers_->byteSize() : 0;
      return headers_size + sizeof(*this) + body_.size() + trailers_size;
    }
  };

  // Looks for a response that has been varied. Only called from lookup.
  Entry varyLookup(const LookupRequest& request,
                   const Http::ResponseHeaderMapPtr& response_headers);

  // A list of headers that we do not want to update upon validation
  // We skip these headers because either it's updated by other application logic
  // or they are fall into categories defined in the IETF doc below
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-cache-18.html s3.2
  static const absl::flat_hash_set<Http::LowerCaseString> headersNotToUpdate();

  typedef SimpleLRUCache<Key, Entry, MessageUtil, MessageUtil> LRUCache;

public:
  // HttpCache
  SimpleHttpCache() { lru_cache_ = std::make_unique<LRUCache>(kSimpleHttpCacheDefaultSize); }

  LookupContextPtr makeLookupContext(LookupRequest&& request,
                                     Http::StreamDecoderFilterCallbacks& callbacks) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context,
                                     Http::StreamEncoderFilterCallbacks& callbacks) override;
  void updateHeaders(const LookupContext& lookup_context,
                     const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata) override;
  CacheInfo cacheInfo() const override;

  Entry lookup(const LookupRequest& request);
  void insert(const Key& key, Http::ResponseHeaderMapPtr&& response_headers,
              ResponseMetadata&& metadata, std::string&& body,
              Http::ResponseTrailerMapPtr&& trailers);

  // Inserts a response that has been varied on certain headers.
  void varyInsert(const Key& request_key, Http::ResponseHeaderMapPtr&& response_headers,
                  ResponseMetadata&& metadata, std::string&& body,
                  const Http::RequestHeaderMap& request_headers,
                  const VaryAllowList& vary_allow_list, Http::ResponseTrailerMapPtr&& trailers);

  // Change the maximum size of the cache to the specified number of bytes.
  // If necessary, entries will be evicted to comply with the new size.
  void setMaxSize(int64_t bytes);

  absl::Mutex mutex_;
  std::unique_ptr<LRUCache> lru_cache_ ABSL_GUARDED_BY(mutex_);
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

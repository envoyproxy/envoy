#pragma once

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/cache/http_cache.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

// included to make code_format happy
#include "envoy/extensions/cache/simple_http_cache/v3alpha/config.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Example cache backend that never evicts. Not suitable for production use.
class SimpleHttpCache : public HttpCache {
private:
  struct Entry {
    Http::ResponseHeaderMapPtr response_headers_;
    ResponseMetadata metadata_;
    std::string body_;
  };

  // Looks for a response that has been varied. Only called from lookup.
  Entry varyLookup(const LookupRequest& request,
                   const Http::ResponseHeaderMapPtr& response_headers);

public:
  // HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) override;
  void updateHeaders(const LookupContext& lookup_context,
                     const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata) override;
  CacheInfo cacheInfo() const override;

  Entry lookup(const LookupRequest& request);
  void insert(const Key& key, Http::ResponseHeaderMapPtr&& response_headers,
              ResponseMetadata&& metadata, std::string&& body);

  // Inserts a response that has been varied on certain headers.
  void varyInsert(const Key& request_key, Http::ResponseHeaderMapPtr&& response_headers,
                  ResponseMetadata&& metadata, std::string&& body,
                  const Http::RequestHeaderMap& request_vary_headers);

  absl::Mutex mutex_;
  absl::flat_hash_map<Key, Entry, MessageUtil, MessageUtil> map_ ABSL_GUARDED_BY(mutex_);
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

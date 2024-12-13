#pragma once

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/cache/http_cache.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Example cache backend that never evicts. Not suitable for production use.
class SimpleHttpCache : public HttpCache, public Singleton::Instance {
private:
  struct Entry {
    Http::ResponseHeaderMapPtr response_headers_;
    ResponseMetadata metadata_;
    std::string body_;
    Http::ResponseTrailerMapPtr trailers_;
  };

  // Looks for a response that has been varied. Only called from lookup.
  Entry varyLookup(const LookupRequest& request,
                   const Http::ResponseHeaderMapPtr& response_headers);

  // A list of headers that we do not want to update upon validation
  // We skip these headers because either it's updated by other application logic
  // or they are fall into categories defined in the IETF doc below
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-cache-18.html s3.2
  static const absl::flat_hash_set<Http::LowerCaseString> headersNotToUpdate();

public:
  // HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request,
                                     Http::StreamFilterCallbacks& callbacks) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context,
                                     Http::StreamFilterCallbacks& callbacks) override;
  void updateHeaders(const LookupContext& lookup_context,
                     const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, UpdateHeadersCallback on_complete) override;
  CacheInfo cacheInfo() const override;

  Entry lookup(const LookupRequest& request);
  bool insert(const Key& key, Http::ResponseHeaderMapPtr&& response_headers,
              ResponseMetadata&& metadata, std::string&& body,
              Http::ResponseTrailerMapPtr&& trailers);

  // Inserts a response that has been varied on certain headers.
  bool varyInsert(const Key& request_key, Http::ResponseHeaderMapPtr&& response_headers,
                  ResponseMetadata&& metadata, std::string&& body,
                  const Http::RequestHeaderMap& request_headers,
                  const VaryAllowList& vary_allow_list, Http::ResponseTrailerMapPtr&& trailers);

  absl::Mutex mutex_;
  absl::flat_hash_map<Key, Entry, MessageUtil, MessageUtil> map_ ABSL_GUARDED_BY(mutex_);
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

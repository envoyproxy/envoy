#pragma once

#include <unordered_map>

#include "common/buffer/buffer_impl.h"
#include "common/common/thread.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/cache/http_cache.h"

#include "absl/base/thread_annotations.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Example cache backend that never evicts. Not suitable for production use.
class SimpleHttpCache : public HttpCache {
private:
  struct Entry {
    Http::HeaderMapPtr response_headers;
    std::string body;
  };

public:
  // HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request) override;
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) override;
  void updateHeaders(LookupContextPtr&& lookup_context,
                     Http::HeaderMapPtr&& response_headers) override;
  CacheInfo cacheInfo() const override;

  Entry lookup(const LookupRequest& request);
  void insert(const Key& key, Http::HeaderMapPtr&& response_headers, std::string&& body);

  mutable Thread::MutexBasicLockable mutex_;
  std::unordered_map<Key, Entry, MessageUtil, MessageUtil> map_ GUARDED_BY(mutex_);
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#pragma once

#include <chrono>
#include <cstdint>

#include "envoy/common/time.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/cache/cache_policy/cache_policy.h"
#include "source/extensions/filters/http/cache/key.pb.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using ::Envoy::Extensions::Cache::CachePolicyCallbacks;

class MockCachePolicy : public CachePolicy {
public:
  MOCK_METHOD(Key, createCacheKey, (const Http::RequestHeaderMap& request_headers), (override));
  MOCK_METHOD(bool, requestCacheable,
              (const Http::RequestHeaderMap& request_headers,
               const RequestCacheControl& cache_control),
              (override));
  MOCK_METHOD(bool, responseCacheable,
              (const Http::RequestHeaderMap& request_headers,
               const Http::ResponseHeaderMap& response_headers,
               const ResponseCacheControl& response_cache_control,
               const VaryAllowList& vary_allow_list),
              (override));
  MOCK_METHOD(CacheEntryUsability, computeCacheEntryUsability,
              (const Http::RequestHeaderMap& request_headers,
               const Http::ResponseHeaderMap& cached_response_headers,
               const RequestCacheControl& request_cache_control,
               const ResponseCacheControl& cached_response_cache_control,
               const uint64_t content_length, const ResponseMetadata& cached_metadata,
               SystemTime now),
              (override));
  MOCK_METHOD(void, setCallbacks, (CachePolicyCallbacks & callbacks), (override));
};

class MockCachePolicyCallbacks : public CachePolicyCallbacks {
public:
  MOCK_METHOD(const StreamInfo::FilterStateSharedPtr&, filterState, (), (override));
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "source/extensions/filters/http/cache/http_cache.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class MockHttpCache : public HttpCache {
public:
  MOCK_METHOD(LookupContextPtr, makeLookupContext,
              (LookupRequest && request, Http::StreamDecoderFilterCallbacks& callbacks));
  MOCK_METHOD(InsertContextPtr, makeInsertContext,
              (LookupContextPtr && lookup_context, Http::StreamEncoderFilterCallbacks& callbacks));
  MOCK_METHOD(void, updateHeaders,
              (const LookupContext& lookup_context, const Http::ResponseHeaderMap& response_headers,
               const ResponseMetadata& metadata, std::function<void(bool)> on_complete));
  MOCK_METHOD(CacheInfo, cacheInfo, (), (const));
};

class MockLookupContext : public LookupContext {
public:
  MOCK_METHOD(void, getHeaders, (LookupHeadersCallback && cb));
  MOCK_METHOD(void, getBody, (const AdjustedByteRange& range, LookupBodyCallback&& cb));
  MOCK_METHOD(void, getTrailers, (LookupTrailersCallback && cb));
  MOCK_METHOD(void, onDestroy, ());
};

class MockInsertContext : public InsertContext {
public:
  MOCK_METHOD(void, insertHeaders,
              (const Http::ResponseHeaderMap& response_headers, const ResponseMetadata& metadata,
               InsertCallback insert_complete, bool end_stream));
  MOCK_METHOD(void, insertBody,
              (const Buffer::Instance& fragment, InsertCallback ready_for_next_fragment,
               bool end_stream));
  MOCK_METHOD(void, insertTrailers,
              (const Http::ResponseTrailerMap& trailers, InsertCallback insert_complete));
  MOCK_METHOD(void, onDestroy, ());
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "source/extensions/filters/http/cache/http_cache.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class MockInsertContext : public InsertContext {
public:
  MOCK_METHOD(void, insertHeaders,
              (const Http::ResponseHeaderMap& response_headers, const ResponseMetadata& metadata,
               bool end_stream),
              (override));

  MOCK_METHOD(void, insertBody,
              (const Buffer::Instance& chunk, InsertCallback ready_for_next_chunk, bool end_stream),
              (override));

  MOCK_METHOD(void, insertTrailers, (const Http::ResponseTrailerMap& trailers), (override));

  MOCK_METHOD(void, onDestroy, (), (override));
};

class MockLookupContext : public LookupContext {
public:
  MOCK_METHOD(void, getHeaders, (LookupHeadersCallback && cb), (override));

  MOCK_METHOD(void, getBody, (const AdjustedByteRange& range, LookupBodyCallback&& cb), (override));

  MOCK_METHOD(void, getTrailers, (LookupTrailersCallback && cb), (override));

  MOCK_METHOD(void, onDestroy, (), (override));
};

class MockHttpCache : public HttpCache {
public:
  MOCK_METHOD(LookupContextPtr, makeLookupContext,
              (LookupRequest && request, Http::StreamDecoderFilterCallbacks& callbacks),
              (override));
  MOCK_METHOD(InsertContextPtr, makeInsertContext,
              (LookupContextPtr && lookup_context, Http::StreamEncoderFilterCallbacks& callbacks),
              (override));
  MOCK_METHOD(void, updateHeaders,
              (const LookupContext& lookup_context, const Http::ResponseHeaderMap& response_headers,
               const ResponseMetadata& metadata),
              (override));
  MOCK_METHOD(CacheInfo, cacheInfo, (), (const override));

  MockHttpCache() {
    // Return uninteresting mock contexts instead of nullptr by default, so
    // tests don't crash on unexpected calls.
    ON_CALL(*this, makeLookupContext)
        .WillByDefault(testing::InvokeWithoutArgs(std::make_unique<MockLookupContext>));
    ON_CALL(*this, makeInsertContext)
        .WillByDefault(testing::InvokeWithoutArgs(std::make_unique<MockInsertContext>));
  }
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

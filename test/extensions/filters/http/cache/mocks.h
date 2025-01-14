#pragma once

#include "source/extensions/filters/http/cache/http_cache.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

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

class MockHttpCache : public HttpCache {
public:
  MOCK_METHOD(LookupContextPtr, makeLookupContext,
              (LookupRequest && request, Http::StreamFilterCallbacks& callbacks));
  MOCK_METHOD(InsertContextPtr, makeInsertContext,
              (LookupContextPtr && lookup_context, Http::StreamFilterCallbacks& callbacks));
  MOCK_METHOD(void, updateHeaders,
              (const LookupContext& lookup_context, const Http::ResponseHeaderMap& response_headers,
               const ResponseMetadata& metadata, absl::AnyInvocable<void(bool)> on_complete));
  MOCK_METHOD(CacheInfo, cacheInfo, (), (const));
  MockLookupContext* mockLookupContext() {
    ASSERT(mock_lookup_context_ == nullptr);
    mock_lookup_context_ = std::make_unique<MockLookupContext>();
    EXPECT_CALL(*mock_lookup_context_, onDestroy());
    EXPECT_CALL(*this, makeLookupContext)
        .WillOnce([this](LookupRequest&&,
                         Http::StreamFilterCallbacks&) -> std::unique_ptr<LookupContext> {
          auto ret = std::move(mock_lookup_context_);
          mock_lookup_context_ = nullptr;
          return ret;
        });
    return mock_lookup_context_.get();
  }
  MockInsertContext* mockInsertContext() {
    ASSERT(mock_insert_context_ == nullptr);
    mock_insert_context_ = std::make_unique<MockInsertContext>();
    EXPECT_CALL(*mock_insert_context_, onDestroy());
    EXPECT_CALL(*this, makeInsertContext)
        .WillOnce([this](LookupContextPtr&& lookup_context,
                         Http::StreamFilterCallbacks&) -> std::unique_ptr<InsertContext> {
          lookup_context->onDestroy();
          auto ret = std::move(mock_insert_context_);
          mock_insert_context_ = nullptr;
          return ret;
        });
    return mock_insert_context_.get();
  }
  std::unique_ptr<MockLookupContext> mock_lookup_context_;
  std::unique_ptr<MockInsertContext> mock_insert_context_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "envoy/event/dispatcher.h"

#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache/cache_filter.h"
#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"
#include "source/extensions/http/cache/simple_http_cache/simple_http_cache.h"

#include "test/extensions/filters/http/cache/common.h"
#include "test/extensions/filters/http/cache/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::testing::IsNull;
using ::testing::NotNull;

class CacheFilterTest : public ::testing::Test {
protected:
  // The filter has to be created as a shared_ptr to enable shared_from_this() which is used in the
  // cache callbacks.
  CacheFilterSharedPtr makeFilter(std::shared_ptr<HttpCache> cache, bool auto_destroy = true) {
    std::shared_ptr<CacheFilter> filter(new CacheFilter(config_, /*stats_prefix=*/"",
                                                        context_.scope(), context_.timeSource(),
                                                        cache),
                                        [auto_destroy](CacheFilter* f) {
                                          if (auto_destroy) {
                                            f->onDestroy();
                                          }
                                          delete f;
                                        });
    filter_state_ = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::FilterChain);
    filter->setDecoderFilterCallbacks(decoder_callbacks_);
    filter->setEncoderFilterCallbacks(encoder_callbacks_);
    return filter;
  }

  void SetUp() override {
    ON_CALL(encoder_callbacks_, dispatcher()).WillByDefault(::testing::ReturnRef(*dispatcher_));
    ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(::testing::ReturnRef(*dispatcher_));
    ON_CALL(decoder_callbacks_.stream_info_, filterState())
        .WillByDefault(::testing::ReturnRef(filter_state_));
    // Initialize the time source (otherwise it returns the real time)
    time_source_.setSystemTime(std::chrono::hours(1));
    // Use the initialized time source to set the response date header
    response_headers_.setDate(formatter_.now(time_source_));
  }

  absl::StatusOr<const CacheFilterLoggingInfo> cacheFilterLoggingInfo() {
    if (!filter_state_->hasData<CacheFilterLoggingInfo>(CacheFilterLoggingInfo::FilterStateKey)) {
      return absl::NotFoundError("cacheFilterLoggingInfo not found");
    }
    return *filter_state_->getDataReadOnly<CacheFilterLoggingInfo>(
        CacheFilterLoggingInfo::FilterStateKey);
  }

  absl::StatusOr<LookupStatus> lookupStatus() {
    absl::StatusOr<const CacheFilterLoggingInfo> info_or = cacheFilterLoggingInfo();
    if (info_or.ok()) {
      return info_or.value().lookupStatus();
    }
    return info_or.status();
  }

  absl::StatusOr<InsertStatus> insertStatus() {
    absl::StatusOr<const CacheFilterLoggingInfo> info_or = cacheFilterLoggingInfo();
    if (info_or.ok()) {
      return info_or.value().insertStatus();
    }
    return info_or.status();
  }

  void testDecodeRequestMiss(CacheFilterSharedPtr filter) {
    // The filter should not encode any headers or data as no cached response exists.
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);

    // The filter should stop decoding iteration when decodeHeaders is called as a cache lookup is
    // in progress.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // The filter should continue decoding when the cache lookup result (miss) is ready.
    EXPECT_CALL(decoder_callbacks_, continueDecoding);

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
  }

  void testDecodeRequestHitNoBody(CacheFilterSharedPtr filter) {
    // The filter should encode cached headers.
    EXPECT_CALL(
        decoder_callbacks_,
        encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                      HeaderHasValueRef(Http::CustomHeaders::get().Age, age)),
                       true));

    // The filter should not encode any data as the response has no body.
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);

    // The filter should stop decoding iteration when decodeHeaders is called as a cache lookup is
    // in progress.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // The filter should not continue decoding when the cache lookup result is ready, as the
    // expected result is a hit.
    EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
  }

  void testDecodeRequestHitWithBody(CacheFilterSharedPtr filter, std::string body) {
    // The filter should encode cached headers.
    EXPECT_CALL(
        decoder_callbacks_,
        encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                      HeaderHasValueRef(Http::CustomHeaders::get().Age, age)),
                       false));

    // The filter should encode cached data.
    EXPECT_CALL(
        decoder_callbacks_,
        encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));

    // The filter should stop decoding iteration when decodeHeaders is called as a cache lookup is
    // in progress.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // The filter should not continue decoding when the cache lookup result is ready, as the
    // expected result is a hit.
    EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    // The posted lookup callback will cause another callback to be posted (when getBody() is
    // called) which should also be invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
  }

  void waitBeforeSecondRequest() { time_source_.advanceTimeWait(delay_); }

  std::shared_ptr<SimpleHttpCache> simple_cache_ = std::make_shared<SimpleHttpCache>();
  envoy::extensions::filters::http::cache::v3::CacheConfig config_;
  std::shared_ptr<StreamInfo::FilterState> filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Event::SimulatedTimeSystem time_source_;
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {":scheme", "https"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"cache-control", "public,max-age=3600"}};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
  const Seconds delay_ = Seconds(10);
  const std::string age = std::to_string(delay_.count());
};

TEST_F(CacheFilterTest, FilterIsBeingDestroyed) {
  CacheFilterSharedPtr filter = makeFilter(simple_cache_, false);
  filter->onDestroy();
  // decodeHeaders should do nothing... at least make sure it doesn't crash.
  filter->decodeHeaders(request_headers_, true);
}

TEST_F(CacheFilterTest, UncacheableRequest) {
  request_headers_.setHost("UncacheableRequest");

  // POST requests are uncacheable
  request_headers_.setMethod(Http::Headers::get().MethodValues.Post);

  for (int request = 1; request <= 2; request++) {
    // Create filter for the request
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Decode request headers
    // The filter should not encode any headers or data as no cached response exists.
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);

    // Uncacheable requests should bypass the cache filter-> No cache lookups should be initiated.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header
    EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::RequestNotCacheable));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::NoInsertRequestNotCacheable));
  }
}

TEST_F(CacheFilterTest, UncacheableResponse) {
  request_headers_.setHost("UncacheableResponse");

  // Responses with "Cache-Control: no-store" are uncacheable
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-store");

  for (int request = 1; request <= 2; request++) {
    // Create filter for the request.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response headers.
    EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::NoInsertResponseNotCacheable));
  }
}

TEST_F(CacheFilterTest, CacheMiss) {
  for (int request = 1; request <= 2; request++) {
    // Each iteration a request is sent to a different host, therefore the second one is a miss
    request_headers_.setHost("CacheMiss" + std::to_string(request));

    // Create filter for request 1
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response header
    EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertSucceeded));
  }
  // Clear events off the dispatcher.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(CacheFilterTest, Disabled) {
  request_headers_.setHost("CacheDisabled");
  CacheFilterSharedPtr filter = makeFilter(std::shared_ptr<HttpCache>{});
  EXPECT_EQ(filter->decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);
}

TEST_F(CacheFilterTest, CacheMissWithTrailers) {
  request_headers_.setHost("CacheMissWithTrailers");
  const std::string body = "abc";
  Buffer::OwnedImpl body_buffer(body);
  Http::TestResponseTrailerMapImpl trailers;

  for (int request = 1; request <= 2; request++) {
    // Each iteration a request is sent to a different host, therefore the second one is a miss
    request_headers_.setHost("CacheMissWithTrailers" + std::to_string(request));

    // Create filter for request 1
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response header
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->encodeData(body_buffer, false), Http::FilterDataStatus::Continue);
    EXPECT_EQ(filter->encodeTrailers(trailers), Http::FilterTrailersStatus::Continue);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertSucceeded));
  }
  // Clear events off the dispatcher.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(CacheFilterTest, CacheHitNoBody) {
  request_headers_.setHost("CacheHitNoBody");

  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response headers.
    EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
  }
  waitBeforeSecondRequest();
  {
    // Create filter for request 2.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestHitNoBody(filter);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheHit));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::NoInsertCacheHit));
  }
}

TEST_F(CacheFilterTest, CacheHitWithBody) {
  request_headers_.setHost("CacheHitWithBody");
  const std::string body = "abc";

  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response.
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->encodeData(buffer, true), Http::FilterDataStatus::Continue);
    // The cache insertBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertSucceeded));
  }
  waitBeforeSecondRequest();
  {
    // Create filter for request 2
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestHitWithBody(filter, body);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheHit));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::NoInsertCacheHit));
  }
}

TEST_F(CacheFilterTest, WatermarkEventsAreSentIfCacheBlocksStreamAndLimitExceeded) {
  request_headers_.setHost("CacheHitWithBody");
  const std::string body1 = "abcde";
  const std::string body2 = "fghij";
  // Set the buffer limit to 2 bytes to ensure we send watermark events.
  EXPECT_CALL(encoder_callbacks_, encoderBufferLimit()).WillRepeatedly(::testing::Return(2));
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  auto mock_lookup_context = std::make_unique<MockLookupContext>();
  auto mock_insert_context = std::make_unique<MockInsertContext>();
  EXPECT_CALL(*mock_http_cache, makeLookupContext(_, _))
      .WillOnce([&](LookupRequest&&,
                    Http::StreamDecoderFilterCallbacks&) -> std::unique_ptr<LookupContext> {
        return std::move(mock_lookup_context);
      });
  EXPECT_CALL(*mock_http_cache, makeInsertContext(_, _))
      .WillOnce([&](LookupContextPtr&&,
                    Http::StreamEncoderFilterCallbacks&) -> std::unique_ptr<InsertContext> {
        return std::move(mock_insert_context);
      });
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    cb(LookupResult{});
  });
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete, bool) { insert_complete(true); });
  InsertCallback captured_insert_body_callback;
  // The first time insertBody is called, block until the test is ready to call it.
  // For completion chunk, complete immediately.
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, false))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        EXPECT_THAT(captured_insert_body_callback, IsNull());
        captured_insert_body_callback = ready_for_next_chunk;
      });
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, true))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        ready_for_next_chunk(true);
      });
  EXPECT_CALL(*mock_insert_context, onDestroy());
  EXPECT_CALL(*mock_lookup_context, onDestroy());
  {
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache);

    testDecodeRequestMiss(filter);

    // Encode response.
    response_headers_.setContentLength(body1.size() + body2.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    // The insertHeaders callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());
    // Write the body in two pieces - the first one should exceed the watermark and
    // send a high watermark event.
    Buffer::OwnedImpl body1buf(body1);
    Buffer::OwnedImpl body2buf(body2);
    EXPECT_EQ(filter->encodeData(body1buf, false), Http::FilterDataStatus::Continue);
    EXPECT_EQ(filter->encodeData(body2buf, true), Http::FilterDataStatus::Continue);
    ASSERT_THAT(captured_insert_body_callback, NotNull());
    // When the cache releases, a low watermark event should be sent.
    EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());
    captured_insert_body_callback(true);
    // The cache insertBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertSucceeded));
  }
}

TEST_F(CacheFilterTest, FilterDestroyedWhileWatermarkedSendsLowWatermarkEvent) {
  request_headers_.setHost("CacheHitWithBody");
  const std::string body1 = "abcde";
  const std::string body2 = "fghij";
  // Set the buffer limit to 2 bytes to ensure we send watermark events.
  EXPECT_CALL(encoder_callbacks_, encoderBufferLimit()).WillRepeatedly(::testing::Return(2));
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  auto mock_lookup_context = std::make_unique<MockLookupContext>();
  auto mock_insert_context = std::make_unique<MockInsertContext>();
  EXPECT_CALL(*mock_http_cache, makeLookupContext(_, _))
      .WillOnce([&](LookupRequest&&,
                    Http::StreamDecoderFilterCallbacks&) -> std::unique_ptr<LookupContext> {
        return std::move(mock_lookup_context);
      });
  EXPECT_CALL(*mock_http_cache, makeInsertContext(_, _))
      .WillOnce([&](LookupContextPtr&&,
                    Http::StreamEncoderFilterCallbacks&) -> std::unique_ptr<InsertContext> {
        return std::move(mock_insert_context);
      });
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    cb(LookupResult{});
  });
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete, bool) { insert_complete(true); });
  InsertCallback captured_insert_body_callback;
  // The first time insertBody is called, block until the test is ready to call it.
  // Cache aborts, so there is no second call.
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, false))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        EXPECT_THAT(captured_insert_body_callback, IsNull());
        captured_insert_body_callback = ready_for_next_chunk;
      });
  EXPECT_CALL(*mock_insert_context, onDestroy());
  EXPECT_CALL(*mock_lookup_context, onDestroy());
  {
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache, false);

    testDecodeRequestMiss(filter);

    // Encode response.
    response_headers_.setContentLength(body1.size() + body2.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    // The insertHeaders callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());
    // Write the body in two pieces - the first one should exceed the watermark and
    // send a high watermark event.
    Buffer::OwnedImpl body1buf(body1);
    Buffer::OwnedImpl body2buf(body2);
    EXPECT_EQ(filter->encodeData(body1buf, false), Http::FilterDataStatus::Continue);
    EXPECT_EQ(filter->encodeData(body2buf, true), Http::FilterDataStatus::Continue);
    ASSERT_THAT(captured_insert_body_callback, NotNull());
    // When the filter is destroyed, a low watermark event should be sent.
    EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());
    filter->onDestroy();
    filter.reset();
    captured_insert_body_callback(false);
    // The cache insertBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }
}

TEST_F(CacheFilterTest, CacheInsertAbortedByCache) {
  request_headers_.setHost("CacheHitWithBody");
  const std::string body = "abc";
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  auto mock_lookup_context = std::make_unique<MockLookupContext>();
  auto mock_insert_context = std::make_unique<MockInsertContext>();
  EXPECT_CALL(*mock_http_cache, makeLookupContext(_, _))
      .WillOnce([&](LookupRequest&&,
                    Http::StreamDecoderFilterCallbacks&) -> std::unique_ptr<LookupContext> {
        return std::move(mock_lookup_context);
      });
  EXPECT_CALL(*mock_http_cache, makeInsertContext(_, _))
      .WillOnce([&](LookupContextPtr&&,
                    Http::StreamEncoderFilterCallbacks&) -> std::unique_ptr<InsertContext> {
        return std::move(mock_insert_context);
      });
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    cb(LookupResult{});
  });
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete, bool) { insert_complete(true); });
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, true))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        ready_for_next_chunk(false);
      });
  EXPECT_CALL(*mock_insert_context, onDestroy());
  EXPECT_CALL(*mock_lookup_context, onDestroy());
  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache);

    testDecodeRequestMiss(filter);

    // Encode response.
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->encodeData(buffer, true), Http::FilterDataStatus::Continue);
    // The cache insertBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertAbortedByCache));
  }
}

TEST_F(CacheFilterTest, FilterDeletedWhileIncompleteCacheWriteInQueueShouldAbandonWrite) {
  request_headers_.setHost("CacheHitWithBody");
  const std::string body = "abc";
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  std::weak_ptr<MockHttpCache> weak_cache_pointer = mock_http_cache;
  auto mock_lookup_context = std::make_unique<MockLookupContext>();
  auto mock_insert_context = std::make_unique<MockInsertContext>();
  EXPECT_CALL(*mock_http_cache, makeLookupContext(_, _))
      .WillOnce([&](LookupRequest&&,
                    Http::StreamDecoderFilterCallbacks&) -> std::unique_ptr<LookupContext> {
        return std::move(mock_lookup_context);
      });
  EXPECT_CALL(*mock_http_cache, makeInsertContext(_, _))
      .WillOnce([&](LookupContextPtr&&,
                    Http::StreamEncoderFilterCallbacks&) -> std::unique_ptr<InsertContext> {
        return std::move(mock_insert_context);
      });
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    cb(LookupResult{});
  });
  InsertCallback captured_insert_header_callback;
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete,
                    bool) { captured_insert_header_callback = insert_complete; });
  EXPECT_CALL(*mock_insert_context, onDestroy());
  EXPECT_CALL(*mock_lookup_context, onDestroy());
  {
    // Create filter for request 1 and move the local shared_ptr,
    // transferring ownership to the filter.
    CacheFilterSharedPtr filter = makeFilter(std::move(mock_http_cache));

    testDecodeRequestMiss(filter);

    // Encode header of response.
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    // Destroy the filter prematurely.
  }
  ASSERT_THAT(captured_insert_header_callback, NotNull());
  EXPECT_THAT(weak_cache_pointer.lock(), NotNull())
      << "cache instance was unexpectedly destroyed when filter was destroyed";
  captured_insert_header_callback(true);
  // The callback should be posted to the dispatcher.
  // Run events on the dispatcher so that the callback is invoked,
  // where it should now do nothing due to the filter being destroyed.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(CacheFilterTest, FilterDeletedWhileCompleteCacheWriteInQueueShouldContinueWrite) {
  request_headers_.setHost("CacheHitWithBody");
  const std::string body = "abc";
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  auto mock_lookup_context = std::make_unique<MockLookupContext>();
  auto mock_insert_context = std::make_unique<MockInsertContext>();
  EXPECT_CALL(*mock_http_cache, makeLookupContext(_, _))
      .WillOnce([&](LookupRequest&&,
                    Http::StreamDecoderFilterCallbacks&) -> std::unique_ptr<LookupContext> {
        return std::move(mock_lookup_context);
      });
  EXPECT_CALL(*mock_http_cache, makeInsertContext(_, _))
      .WillOnce([&](LookupContextPtr&&,
                    Http::StreamEncoderFilterCallbacks&) -> std::unique_ptr<InsertContext> {
        return std::move(mock_insert_context);
      });
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    cb(LookupResult{});
  });
  InsertCallback captured_insert_header_callback;
  InsertCallback captured_insert_body_callback;
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete,
                    bool) { captured_insert_header_callback = insert_complete; });
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, true))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        captured_insert_body_callback = ready_for_next_chunk;
      });
  EXPECT_CALL(*mock_insert_context, onDestroy());
  EXPECT_CALL(*mock_lookup_context, onDestroy());
  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache);

    testDecodeRequestMiss(filter);

    // Encode response.
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->encodeData(buffer, true), Http::FilterDataStatus::Continue);
  }
  // Header callback should be captured, body callback should not yet since the
  // queue has not reached that chunk.
  ASSERT_THAT(captured_insert_header_callback, NotNull());
  ASSERT_THAT(captured_insert_body_callback, IsNull());
  // The callback should be posted to the dispatcher.
  captured_insert_header_callback(true);
  // Run events on the dispatcher so that the callback is invoked,
  // where it should now proceed to write the body chunk, since the
  // write is still completable.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // So the mock should now be writing the body.
  ASSERT_THAT(captured_insert_body_callback, NotNull());
  captured_insert_body_callback(true);
  // The callback should be posted to the dispatcher.
  // Run events on the dispatcher so that the callback is invoked,
  // where it should now do nothing due to the filter being destroyed.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(CacheFilterTest, SuccessfulValidation) {
  request_headers_.setHost("SuccessfulValidation");
  const std::string body = "abc";
  const std::string etag = "abc123";
  const std::string last_modified_date = formatter_.now(time_source_);
  {
    // Create filter for request 1
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response
    // Add Etag & Last-Modified headers to the response for validation
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, last_modified_date);

    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->encodeData(buffer, true), Http::FilterDataStatus::Continue);
    // The cache getBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
  }
  waitBeforeSecondRequest();
  {
    // Create filter for request 2
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make request require validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

    // Decoding the request should find a cached response that requires validation.
    // As far as decoding the request is concerned, this is the same as a cache miss with the
    // exception of injecting validation precondition headers.
    testDecodeRequestMiss(filter);

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", etag}, {"if-modified-since", last_modified_date}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));

    // Encode 304 response
    // Advance time to make sure the cached date is updated with the 304 date
    const std::string not_modified_date = formatter_.now(time_source_);
    Http::TestResponseHeaderMapImpl not_modified_response_headers = {{":status", "304"},
                                                                     {"date", not_modified_date}};

    // The filter should stop encoding iteration when encodeHeaders is called as a cached response
    // is being fetched and added to the encoding stream. StopIteration does not stop encodeData of
    // the same filter from being called
    EXPECT_EQ(filter->encodeHeaders(not_modified_response_headers, true),
              Http::FilterHeadersStatus::StopIteration);

    // Check for the cached response headers with updated date
    Http::TestResponseHeaderMapImpl updated_response_headers = response_headers_;
    updated_response_headers.setDate(not_modified_date);
    EXPECT_THAT(not_modified_response_headers, IsSupersetOfHeaders(updated_response_headers));

    // A 304 response should not have a body, so encodeData should not be called
    // However, if a body is present by mistake, encodeData should stop iteration until
    // encoding the cached response is done
    Buffer::OwnedImpl not_modified_body;
    EXPECT_EQ(filter->encodeData(not_modified_body, true),
              Http::FilterDataStatus::StopIterationAndBuffer);

    // The filter should add the cached response body to encoded data.
    Buffer::OwnedImpl buffer(body);
    EXPECT_CALL(
        encoder_callbacks_,
        addEncodedData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));

    // The cache getBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::StaleHitWithSuccessfulValidation));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::HeaderUpdate));
  }
}

TEST_F(CacheFilterTest, UnsuccessfulValidation) {
  request_headers_.setHost("UnsuccessfulValidation");
  const std::string body = "abc";
  const std::string etag = "abc123";
  const std::string last_modified_date = formatter_.now(time_source_);
  {
    // Create filter for request 1
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response
    // Add Etag & Last-Modified headers to the response for validation.
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, last_modified_date);

    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->encodeData(buffer, true), Http::FilterDataStatus::Continue);
    // The cache getBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
  }
  waitBeforeSecondRequest();
  {
    // Create filter for request 2.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make request require validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

    // Decoding the request should find a cached response that requires validation.
    // As far as decoding the request is concerned, this is the same as a cache miss with the
    // exception of injecting validation precondition headers.
    testDecodeRequestMiss(filter);

    // Make sure validation conditional headers are added.
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", etag}, {"if-modified-since", last_modified_date}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));

    // Encode new response.
    // Change the status code to make sure new headers are served, not the cached ones.
    response_headers_.setStatus(204);

    // The filter should not stop encoding iteration as this is a new response.
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    Buffer::OwnedImpl new_body;
    EXPECT_EQ(filter->encodeData(new_body, true), Http::FilterDataStatus::Continue);

    // The response headers should have the new status.
    EXPECT_THAT(response_headers_, HeaderHasValueRef(Http::Headers::get().Status, "204"));

    // The filter should not encode any data.
    EXPECT_CALL(encoder_callbacks_, addEncodedData).Times(0);

    // If a cache getBody callback is made, it should be posted to the dispatcher.
    // Run events on the dispatcher so that any available callbacks are invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::StaleHitWithFailedValidation));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertSucceeded));
  }
}

TEST_F(CacheFilterTest, SingleSatisfiableRange) {
  request_headers_.setHost("SingleSatisfiableRange");
  const std::string body = "abc";

  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response.
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->encodeData(buffer, true), Http::FilterDataStatus::Continue);
    // The cache getBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
  }
  waitBeforeSecondRequest();
  {
    // Add range info to headers.
    request_headers_.addReference(Http::Headers::get().Range, "bytes=-2");

    response_headers_.setStatus(static_cast<uint64_t>(Http::Code::PartialContent));
    response_headers_.addReference(Http::Headers::get().ContentRange, "bytes 1-2/3");
    response_headers_.setContentLength(2);

    // Create filter for request 2
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Decode request 2 header
    EXPECT_CALL(
        decoder_callbacks_,
        encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                      HeaderHasValueRef(Http::CustomHeaders::get().Age, age)),
                       false));

    EXPECT_CALL(
        decoder_callbacks_,
        encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq("bc")), true));
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    // The posted lookup callback will cause another callback to be posted (when getBody() is
    // called) which should also be invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheHit));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::NoInsertCacheHit));
  }
}

TEST_F(CacheFilterTest, MultipleSatisfiableRanges) {
  request_headers_.setHost("MultipleSatisfiableRanges");
  const std::string body = "abc";

  {
    // Create filter for request 1
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response header
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->encodeData(buffer, true), Http::FilterDataStatus::Continue);
    // The cache getBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
  }
  waitBeforeSecondRequest();
  {
    // Add range info to headers
    // multi-part responses are not supported, 200 expected
    request_headers_.addReference(Http::Headers::get().Range, "bytes=0-1,-2");

    // Create filter for request 2
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Decode request 2 header
    EXPECT_CALL(
        decoder_callbacks_,
        encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                      HeaderHasValueRef(Http::CustomHeaders::get().Age, age)),
                       false));

    EXPECT_CALL(
        decoder_callbacks_,
        encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    // The posted lookup callback will cause another callback to be posted (when getBody() is
    // called) which should also be invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheHit));
  }
}

TEST_F(CacheFilterTest, NotSatisfiableRange) {
  request_headers_.setHost("NotSatisfiableRange");
  const std::string body = "abc";

  {
    // Create filter for request 1
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response header
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->encodeData(buffer, true), Http::FilterDataStatus::Continue);
    // The cache getBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
  }
  waitBeforeSecondRequest();
  {
    // Add range info to headers
    request_headers_.addReference(Http::Headers::get().Range, "bytes=123-");

    response_headers_.setStatus(static_cast<uint64_t>(Http::Code::RangeNotSatisfiable));
    response_headers_.addReference(Http::Headers::get().ContentRange, "bytes */3");
    response_headers_.setContentLength(0);

    // Create filter for request 2
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Decode request 2 header
    EXPECT_CALL(
        decoder_callbacks_,
        encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                      HeaderHasValueRef(Http::CustomHeaders::get().Age, age)),
                       true));

    // 416 response should not have a body, so we don't expect a call to encodeData
    EXPECT_CALL(decoder_callbacks_,
                encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true))
        .Times(0);

    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    // The posted lookup callback will cause another callback to be posted (when getBody() is
    // called) which should also be invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    // This counts as a cache hit: we served an HTTP error, but we
    // correctly got that info from the cache instead of upstream.
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheHit));
  }
}

// Send two identical GET requests with bodies. The CacheFilter will just pass everything through.
TEST_F(CacheFilterTest, GetRequestWithBodyAndTrailers) {
  request_headers_.setHost("GetRequestWithBodyAndTrailers");
  const std::string body = "abc";
  Buffer::OwnedImpl request_buffer(body);
  Http::TestRequestTrailerMapImpl request_trailers;

  for (int i = 0; i < 2; ++i) {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    EXPECT_EQ(filter->decodeHeaders(request_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter->decodeData(request_buffer, false), Http::FilterDataStatus::Continue);
    EXPECT_EQ(filter->decodeTrailers(request_trailers), Http::FilterTrailersStatus::Continue);

    EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::RequestNotCacheable));
  }
}

// Checks the case where a cache lookup callback is posted to the dispatcher, then the CacheFilter
// was deleted (e.g. connection dropped with the client) before the posted callback was executed. In
// this case the CacheFilter should not be accessed after it was deleted, which is ensured by using
// a weak_ptr to the CacheFilter in the posted callback.
// This test may mistakenly pass (false positive) even if the CacheFilter is accessed after
// being deleted, as filter_state_ may be accessed and read as "FilterState::Destroyed" which will
// result in a correct behavior. However, running the test with ASAN sanitizer enabled should
// reliably fail if the CacheFilter is accessed after being deleted.
TEST_F(CacheFilterTest, FilterDeletedBeforePostedCallbackExecuted) {
  request_headers_.setHost("FilterDeletedBeforePostedCallbackExecuted");
  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response headers.
    EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
  }
  {
    // Create filter for request 2.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Call decode headers to start the cache lookup, which should immediately post the callback to
    // the dispatcher.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Destroy the filter
    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::RequestIncomplete));
  }

  // Make sure that onHeaders was not called by making sure no decoder callbacks were made.
  EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);

  // Run events on the dispatcher so that the callback is invoked after the filter deletion.
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
}

TEST_F(CacheFilterTest, LocalReplyDuringLookup) {
  request_headers_.setHost("LocalReplyDuringLookup");
  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response headers.
    EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
  }
  {
    // Create filter for request 2.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Call decode headers to start the cache lookup, which should immediately post the callback to
    // the dispatcher.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // While the lookup callback is still on the dispatcher, simulate a local reply.
    Envoy::Http::TestResponseHeaderMapImpl local_response_headers{{":status", "503"}};
    EXPECT_EQ(filter->encodeHeaders(local_response_headers, true),
              Envoy::Http::FilterHeadersStatus::Continue);

    // Make sure that the filter doesn't try to encode the cached response after processing the
    // local reply.
    EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);

    // Run events on the dispatcher so that the lookup callback is invoked after the local reply.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::RequestIncomplete));
  }
}

// Mark tests with EXPECT_ENVOY_BUG as death tests:
// https://google.github.io/googletest/advanced.html#death-test-naming
using CacheFilterDeathTest = CacheFilterTest;

TEST_F(CacheFilterDeathTest, StreamTimeoutDuringLookup) {
  request_headers_.setHost("StreamTimeoutDuringLookup");
  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response headers.
    EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
  }
  Envoy::Http::TestResponseHeaderMapImpl local_response_headers{{":status", "408"}};
  EXPECT_ENVOY_BUG(
      {
        // Create filter for request 2.
        CacheFilterSharedPtr filter = makeFilter(simple_cache_);

        // Call decode headers to start the cache lookup, which should immediately post the
        // callback to the dispatcher.
        EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
                  Http::FilterHeadersStatus::StopAllIterationAndWatermark);

        // Make sure that the filter doesn't try to encode the cached response after processing
        // the local reply.
        EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);
        EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);

        // While the lookup callback is still on the dispatcher, simulate an idle timeout.
        EXPECT_EQ(filter->encodeHeaders(local_response_headers, true),
                  Envoy::Http::FilterHeadersStatus::Continue);
        // As a death test when ENVOY_BUG crashes, as in debug builds, this will exit here,
        // so we must not perform any required cleanup operations below this point in the block.
        // When ENVOY_BUG does not crash, we can still validate additional things.
        dispatcher_->run(Event::Dispatcher::RunType::Block);

        filter->onStreamComplete();
        EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::RequestIncomplete));
      },
      "Request timed out while cache lookup was outstanding.");

  // Clear out captured lookup lambdas from the dispatcher.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST(LookupStatusDeathTest, ResolveLookupStatusRequireValidationAndInitialIsBug) {
  EXPECT_ENVOY_BUG(
      CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation, FilterState::Initial),
      "Unexpected filter state in requestCacheStatus");
}

TEST(LookupStatusDeathTest, ResolveLookupStatusRequireValidationAndDecodeServingFromCacheIsBug) {
  EXPECT_ENVOY_BUG(CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation,
                                                    FilterState::DecodeServingFromCache),
                   "Unexpected filter state in requestCacheStatus");
}

TEST(LookupStatusDeathTest, ResolveLookupStatusRequireValidationAndDestroyedIsBug) {
  EXPECT_ENVOY_BUG(CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation,
                                                    FilterState::Destroyed),
                   "Unexpected filter state in requestCacheStatus");
}

TEST(LookupStatusTest, ResolveLookupStatusReturnsCorrectStatuses) {
  EXPECT_EQ(CacheFilter::resolveLookupStatus(absl::nullopt, FilterState::Initial),
            LookupStatus::RequestIncomplete);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(absl::nullopt, FilterState::NotServingFromCache),
            LookupStatus::RequestNotCacheable);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(absl::nullopt, FilterState::ValidatingCachedResponse),
            LookupStatus::Unknown);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(absl::nullopt, FilterState::ValidatingCachedResponse),
            LookupStatus::Unknown);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(absl::nullopt, FilterState::DecodeServingFromCache),
            LookupStatus::Unknown);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(absl::nullopt, FilterState::EncodeServingFromCache),
            LookupStatus::Unknown);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(absl::nullopt, FilterState::Destroyed),
            LookupStatus::Unknown);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation,
                                             FilterState::ValidatingCachedResponse),
            LookupStatus::RequestIncomplete);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation,
                                             FilterState::EncodeServingFromCache),
            LookupStatus::StaleHitWithSuccessfulValidation);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation,
                                             FilterState::ResponseServedFromCache),
            LookupStatus::StaleHitWithSuccessfulValidation);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation,
                                             FilterState::NotServingFromCache),
            LookupStatus::StaleHitWithFailedValidation);
  EXPECT_EQ(
      CacheFilter::resolveLookupStatus(CacheEntryStatus::FoundNotModified, FilterState::Destroyed),
      LookupStatus::CacheHit);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(CacheEntryStatus::LookupError, FilterState::Destroyed),
            LookupStatus::LookupError);
}

// A new type alias for a different type of tests that use the exact same class
using ValidationHeadersTest = CacheFilterTest;

TEST_F(ValidationHeadersTest, EtagAndLastModified) {
  request_headers_.setHost("EtagAndLastModified");
  const std::string etag = "abc123";

  // Make request 1 to insert the response into cache
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);
    testDecodeRequestMiss(filter);

    // Add validation headers to the response
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                      formatter_.now(time_source_));

    filter->encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(filter);

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
  }
}

TEST_F(ValidationHeadersTest, EtagOnly) {
  request_headers_.setHost("EtagOnly");
  const std::string etag = "abc123";

  // Make request 1 to insert the response into cache
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);
    testDecodeRequestMiss(filter);

    // Add validation headers to the response
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);

    filter->encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(filter);

    // Make sure validation conditional headers are added
    // If-Modified-Since falls back to date
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
  }
}

TEST_F(ValidationHeadersTest, LastModifiedOnly) {
  request_headers_.setHost("LastModifiedOnly");

  // Make request 1 to insert the response into cache
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);
    testDecodeRequestMiss(filter);

    // Add validation headers to the response
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                      formatter_.now(time_source_));

    filter->encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(filter);

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
  }
}

TEST_F(ValidationHeadersTest, NoEtagOrLastModified) {
  request_headers_.setHost("NoEtagOrLastModified");

  // Make request 1 to insert the response into cache
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);
    testDecodeRequestMiss(filter);
    filter->encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(filter);

    // Make sure validation conditional headers are added
    // If-Modified-Since falls back to date
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
  }
}

TEST_F(ValidationHeadersTest, InvalidLastModified) {
  request_headers_.setHost("InvalidLastModified");

  // Make request 1 to insert the response into cache
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);
    testDecodeRequestMiss(filter);

    // Add validation headers to the response
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, "invalid-date");
    filter->encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(filter);

    // Make sure validation conditional headers are added
    // If-Modified-Since falls back to date
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
  }
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

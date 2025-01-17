#include <functional>

#include "envoy/event/dispatcher.h"

#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache/cache_filter.h"
#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"
#include "source/extensions/http/cache/simple_http_cache/simple_http_cache.h"

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
using ::testing::Gt;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Return;

class CacheFilterTest : public ::testing::Test {
protected:
  // The filter has to be created as a shared_ptr to enable shared_from_this() which is used in the
  // cache callbacks.
  CacheFilterSharedPtr makeFilter(std::shared_ptr<HttpCache> cache, bool auto_destroy = true) {
    auto config = std::make_shared<CacheFilterConfig>(config_, context_.server_factory_context_);
    std::shared_ptr<CacheFilter> filter(new CacheFilter(config, cache),
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
    context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
    ON_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.async_client_,
            start)
        .WillByDefault([this](Http::AsyncClient::StreamCallbacks& callbacks,
                              const Http::AsyncClient::StreamOptions&) {
          int i = mock_upstreams_.size();
          mock_upstreams_.push_back(std::make_unique<NiceMock<Http::MockAsyncClientStream>>());
          mock_upstreams_callbacks_.emplace_back(std::ref(callbacks));
          auto ret = mock_upstreams_.back().get();
          mock_upstreams_headers_sent_.emplace_back();
          ON_CALL(*ret, sendHeaders)
              .WillByDefault([this, i](Http::RequestHeaderMap& headers, bool end_stream) {
                EXPECT_EQ(mock_upstreams_headers_sent_[i], absl::nullopt)
                    << "headers should only be sent once";
                EXPECT_TRUE(end_stream) << "post requests should be bypassing the filter";
                mock_upstreams_headers_sent_[i] = Http::TestRequestHeaderMapImpl();
                mock_upstreams_headers_sent_[i]->copyFrom(headers);
              });
          ON_CALL(*ret, reset).WillByDefault([this, i]() {
            mock_upstreams_callbacks_[i].get().onReset();
          });
          return ret;
        });

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

  void pumpDispatcher() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

  void receiveUpstreamComplete(size_t upstream_index) {
    ASSERT(mock_upstreams_callbacks_.size() > upstream_index);
    mock_upstreams_callbacks_[upstream_index].get().onComplete();
  }

  void
  receiveUpstreamHeaders(size_t upstream_index, Http::ResponseHeaderMap& headers, bool end_stream,
                         testing::Matcher<Http::ResponseHeaderMap&> expected_response_headers = _) {
    ASSERT(mock_upstreams_callbacks_.size() > upstream_index);

    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(expected_response_headers, _));

    mock_upstreams_callbacks_[upstream_index].get().onHeaders(
        std::make_unique<Http::TestResponseHeaderMapImpl>(headers), end_stream);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    if (end_stream) {
      receiveUpstreamComplete(upstream_index);
    }
  }

  // On successful verification, the upstream request gets reset rather than
  // onComplete.
  void receiveUpstreamHeadersWithReset(
      size_t upstream_index, Http::ResponseHeaderMap& headers, bool end_stream,
      testing::Matcher<Http::ResponseHeaderMap&> expected_response_headers = _) {
    ASSERT(mock_upstreams_callbacks_.size() > upstream_index);
    ASSERT(mock_upstreams_.size() > upstream_index);
    EXPECT_CALL(*mock_upstreams_[upstream_index], reset());
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(expected_response_headers, _));
    mock_upstreams_callbacks_[upstream_index].get().onHeaders(
        std::make_unique<Http::TestResponseHeaderMapImpl>(headers), end_stream);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    testing::Mock::VerifyAndClearExpectations(mock_upstreams_[1].get());
  }

  void receiveUpstreamBody(size_t upstream_index, absl::string_view body, bool end_stream) {
    ASSERT(mock_upstreams_callbacks_.size() > upstream_index);
    EXPECT_CALL(decoder_callbacks_, encodeData);
    Buffer::OwnedImpl buf{body};
    mock_upstreams_callbacks_[upstream_index].get().onData(buf, end_stream);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    if (end_stream) {
      receiveUpstreamComplete(upstream_index);
    }
  }

  void receiveUpstreamBodyAfterFilterDestroyed(size_t upstream_index, absl::string_view body,
                                               bool end_stream) {
    // Same as receiveUpstreamBody but without expecting a call to encodeData.
    ASSERT(mock_upstreams_callbacks_.size() > upstream_index);
    Buffer::OwnedImpl buf{body};
    mock_upstreams_callbacks_[upstream_index].get().onData(buf, end_stream);
    if (end_stream) {
      receiveUpstreamComplete(upstream_index);
    }
  }

  void receiveUpstreamTrailers(size_t upstream_index, Http::ResponseTrailerMap& trailers) {
    ASSERT(mock_upstreams_callbacks_.size() > upstream_index);
    EXPECT_CALL(decoder_callbacks_, encodeTrailers_);
    mock_upstreams_callbacks_[upstream_index].get().onTrailers(
        std::make_unique<Http::TestResponseTrailerMapImpl>(trailers));
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    receiveUpstreamComplete(upstream_index);
  }

  void receiveUpstreamTrailersAfterFilterDestroyed(size_t upstream_index,
                                                   Http::ResponseTrailerMap& trailers) {
    ASSERT(mock_upstreams_callbacks_.size() > upstream_index);
    mock_upstreams_callbacks_[upstream_index].get().onTrailers(
        std::make_unique<Http::TestResponseTrailerMapImpl>(trailers));
    receiveUpstreamComplete(upstream_index);
  }

  void populateCommonCacheEntry(size_t upstream_index, CacheFilterSharedPtr filter,
                                absl::string_view body = "",
                                OptRef<Http::ResponseTrailerMap> trailers = absl::nullopt) {
    testDecodeRequestMiss(upstream_index, filter);

    receiveUpstreamHeaders(upstream_index, response_headers_,
                           body.empty() && trailers == absl::nullopt);

    if (!body.empty()) {
      receiveUpstreamBody(upstream_index, body, trailers == absl::nullopt);
    }
    if (trailers) {
      receiveUpstreamTrailers(upstream_index, *trailers);
    }

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    pumpDispatcher();
  }

  void testDecodeRequestMiss(size_t upstream_index, CacheFilterSharedPtr filter) {
    // The filter should stop decoding iteration when decodeHeaders is called as a cache lookup is
    // in progress.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    pumpDispatcher();

    // An upstream request should be sent.
    ASSERT_THAT(mock_upstreams_.size(), Gt(upstream_index));
    ASSERT_THAT(mock_upstreams_headers_sent_.size(), Gt(upstream_index));
    EXPECT_THAT(mock_upstreams_headers_sent_[upstream_index], testing::Optional(request_headers_));
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
    pumpDispatcher();

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
    pumpDispatcher();

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
  std::vector<std::unique_ptr<Http::MockAsyncClientStream>> mock_upstreams_;
  std::vector<std::reference_wrapper<Http::AsyncClient::StreamCallbacks>> mock_upstreams_callbacks_;
  std::vector<absl::optional<Http::TestRequestHeaderMapImpl>> mock_upstreams_headers_sent_;
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

  for (int request = 0; request < 2; request++) {
    std::cerr << "  request " << request << std::endl;
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

  for (int request = 0; request < 2; request++) {
    std::cerr << "  request " << request << std::endl;
    // Create filter for the request.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(request, filter);

    receiveUpstreamHeaders(request, response_headers_, true);

    pumpDispatcher();
    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::NoInsertResponseNotCacheable));
  }
}

TEST_F(CacheFilterTest, CacheMiss) {
  for (int request = 0; request < 2; request++) {
    std::cerr << "  request " << request << std::endl;
    // Each iteration a request is sent to a different host, therefore the second one is a miss
    request_headers_.setHost(absl::StrCat("CacheMiss", request));

    // Create filter for request 1
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(request, filter);

    receiveUpstreamHeaders(request, response_headers_, true);

    pumpDispatcher();
    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertSucceeded));
  }
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
  Http::TestResponseTrailerMapImpl trailers{{"somekey", "somevalue"}};

  for (int request = 0; request < 2; request++) {
    std::cerr << "  request " << request << std::endl;
    // Each iteration a request is sent to a different host, therefore the second one is a miss
    request_headers_.setHost(absl::StrCat("CacheMissWithTrailers", request));

    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(request, filter);

    receiveUpstreamHeaders(request, response_headers_, false);
    receiveUpstreamBody(request, body, false);
    receiveUpstreamTrailers(request, trailers);

    pumpDispatcher();
    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertSucceeded));
  }
}

TEST_F(CacheFilterTest, CacheMissWithTrailersWhenCacheRespondsQuickerThanUpstream) {
  request_headers_.setHost("CacheMissWithTrailers");
  const std::string body = "abc";
  Buffer::OwnedImpl body_buffer(body);
  Http::TestResponseTrailerMapImpl trailers;

  for (int request = 0; request < 2; request++) {
    std::cerr << "  request " << request << std::endl;
    // Each iteration a request is sent to a different host, therefore the second one is a miss
    request_headers_.setHost("CacheMissWithTrailers" + std::to_string(request));

    // Create filter for request 1
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(request, filter);
    receiveUpstreamHeaders(request, response_headers_, false);
    pumpDispatcher();
    receiveUpstreamBody(request, body, false);
    pumpDispatcher();
    receiveUpstreamTrailers(request, trailers);
    pumpDispatcher();

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertSucceeded));
  }
  // Clear events off the dispatcher.
  pumpDispatcher();
}

TEST_F(CacheFilterTest, CacheHitNoBody) {
  request_headers_.setHost("CacheHitNoBody");

  populateCommonCacheEntry(0, makeFilter(simple_cache_));
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

  populateCommonCacheEntry(0, makeFilter(simple_cache_), body);
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
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  MockInsertContext* mock_insert_context = mock_http_cache->mockInsertContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    dispatcher_->post([cb = std::move(cb)]() mutable { std::move(cb)(LookupResult{}, false); });
  });
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete, bool) {
        dispatcher_->post([cb = std::move(insert_complete)]() mutable { std::move(cb)(true); });
      });
  InsertCallback captured_insert_body_callback;
  // The first time insertBody is called, block until the test is ready to call it.
  // For completion chunk, complete immediately.
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, false))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        EXPECT_THAT(captured_insert_body_callback, IsNull());
        captured_insert_body_callback = std::move(ready_for_next_chunk);
      });
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, true))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        dispatcher_->post(
            [cb = std::move(ready_for_next_chunk)]() mutable { std::move(cb)(true); });
      });
  {
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache);

    testDecodeRequestMiss(0, filter);

    // Encode response.
    response_headers_.setContentLength(body1.size() + body2.size());
    receiveUpstreamHeaders(0, response_headers_, false);
    // The insertHeaders callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    pumpDispatcher();

    // TODO(ravenblack): once watermarking is available in async upstreams
    // revisit this test.
    // EXPECT_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());

    // Write the body in two pieces - the first one should exceed the watermark and
    // send a high watermark event.
    receiveUpstreamBody(0, body1, false);
    receiveUpstreamBody(0, body2, true);
    ASSERT_THAT(captured_insert_body_callback, NotNull());

    // TODO(ravenblack): once watermarking is available in async upstreams
    // revisit this test.
    // When the cache releases, a low watermark event should be sent.
    // EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());

    captured_insert_body_callback(true);

    pumpDispatcher();
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
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  MockInsertContext* mock_insert_context = mock_http_cache->mockInsertContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    dispatcher_->post([cb = std::move(cb)]() mutable { std::move(cb)(LookupResult{}, false); });
  });
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete, bool) {
        dispatcher_->post([cb = std::move(insert_complete)]() mutable { std::move(cb)(true); });
      });
  InsertCallback captured_insert_body_callback;
  // The first time insertBody is called, block until the test is ready to call it.
  // Cache aborts, so there is no second call.
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, false))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        EXPECT_THAT(captured_insert_body_callback, IsNull());
        captured_insert_body_callback = std::move(ready_for_next_chunk);
      });
  {
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache, false);

    testDecodeRequestMiss(0, filter);

    // Encode response.
    response_headers_.setContentLength(body1.size() + body2.size());
    receiveUpstreamHeaders(0, response_headers_, false);
    // The insertHeaders callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    pumpDispatcher();

    // TODO(ravenblack): enable watermark testing again once the cache filter's
    // watermark behavior is usable. Currently this is blocked in two ways -
    // async http streams don't support watermarking so we can't slow it down anyway,
    // and populating the cache and streaming to the individual client are still
    // linked, which means slowing it down for the client could also ruin the cache
    // behavior. I intend to make the request that triggers a cache insert turn into
    // a cache streamed read operation once the cache insert begins.
    // EXPECT_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());
    // Write the body in two pieces - the first one should exceed the watermark and
    // send a high watermark event.
    receiveUpstreamBody(0, body1, false);
    pumpDispatcher();
    receiveUpstreamBody(0, body2, true);
    pumpDispatcher();
    ASSERT_THAT(captured_insert_body_callback, NotNull());
    // When the filter is destroyed, a low watermark event should be sent.
    // TODO(ravenblack): enable watermark testing once it works.
    // EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());
    filter->onDestroy();
    filter.reset();
    captured_insert_body_callback(false);
    pumpDispatcher();
  }
}

MATCHER_P2(RangeMatcher, begin, end, "") {
  return testing::ExplainMatchResult(begin, arg.begin(), result_listener) &&
         testing::ExplainMatchResult(end, arg.end(), result_listener);
}

TEST_F(CacheFilterTest, CacheEntryStreamedWithTrailersAndNoContentLengthCanDeliverTrailers) {
  request_headers_.setHost("CacheEntryStreamedWithTrailers");
  const std::string body = "abcde";
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  // response_headers_ intentionally has no content length, LookupResult also has no content length.
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    dispatcher_->post([cb = std::move(cb), this]() mutable {
      std::move(cb)(
          LookupResult{CacheEntryStatus::Ok,
                       std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_),
                       absl::nullopt, absl::nullopt},
          /* end_stream = */ false);
    });
  });
  EXPECT_CALL(*mock_lookup_context, getBody(RangeMatcher(0, Gt(5)), _))
      .WillOnce([&](AdjustedByteRange, LookupBodyCallback&& cb) {
        dispatcher_->post([cb = std::move(cb), &body]() mutable {
          std::move(cb)(std::make_unique<Buffer::OwnedImpl>(body), false);
        });
      });
  EXPECT_CALL(*mock_lookup_context, getBody(RangeMatcher(5, Gt(5)), _))
      .WillOnce([&](AdjustedByteRange, LookupBodyCallback&& cb) {
        dispatcher_->post([cb = std::move(cb)]() mutable { std::move(cb)(nullptr, false); });
      });
  EXPECT_CALL(*mock_lookup_context, getTrailers(_)).WillOnce([&](LookupTrailersCallback&& cb) {
    dispatcher_->post([cb = std::move(cb)]() mutable {
      std::move(cb)(std::make_unique<Http::TestResponseTrailerMapImpl>());
    });
  });
  {
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache);
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    EXPECT_CALL(
        decoder_callbacks_,
        encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq("abcde")), false));
    EXPECT_CALL(decoder_callbacks_, encodeTrailers_(_));

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    // The posted lookup callback will cause another callback to be posted (when getBody() is
    // called) which should also be invoked.
    pumpDispatcher();

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheHit));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::NoInsertCacheHit));
  }
}

TEST_F(CacheFilterTest, OnDestroyBeforeOnHeadersAbortsAction) {
  request_headers_.setHost("CacheHitWithBody");
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    std::unique_ptr<Http::ResponseHeaderMap> response_headers =
        std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_);
    dispatcher_->post([cb = std::move(cb),
                       response_headers = std::move(response_headers)]() mutable {
      std::move(cb)(
          LookupResult{CacheEntryStatus::Ok, std::move(response_headers), 8, absl::nullopt}, false);
    });
  });
  auto filter = makeFilter(mock_http_cache, false);
  EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  filter->onDestroy();
  // Nothing extra should happen when the posted lookup completion resolves, because
  // the filter was destroyed.
  pumpDispatcher();
}

TEST_F(CacheFilterTest, OnDestroyBeforeOnBodyAbortsAction) {
  request_headers_.setHost("CacheHitWithBody");
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    std::unique_ptr<Http::ResponseHeaderMap> response_headers =
        std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_);
    dispatcher_->post([cb = std::move(cb),
                       response_headers = std::move(response_headers)]() mutable {
      std::move(cb)(
          LookupResult{CacheEntryStatus::Ok, std::move(response_headers), 5, absl::nullopt}, false);
    });
  });
  LookupBodyCallback body_callback;
  EXPECT_CALL(*mock_lookup_context, getBody(RangeMatcher(0, 5), _))
      .WillOnce([&](const AdjustedByteRange&, LookupBodyCallback&& cb) {
        body_callback = std::move(cb);
      });
  auto filter = makeFilter(mock_http_cache, false);
  EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  filter->onDestroy();
  ::testing::Mock::VerifyAndClearExpectations(mock_lookup_context);
  EXPECT_THAT(body_callback, NotNull());
  // body_callback should not be called because LookupContext::onDestroy,
  // correctly implemented, should have aborted it.
}

TEST_F(CacheFilterTest, OnDestroyBeforeOnTrailersAbortsAction) {
  request_headers_.setHost("CacheHitWithTrailers");
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    std::unique_ptr<Http::ResponseHeaderMap> response_headers =
        std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_);
    dispatcher_->post([cb = std::move(cb),
                       response_headers = std::move(response_headers)]() mutable {
      std::move(cb)(
          LookupResult{CacheEntryStatus::Ok, std::move(response_headers), 5, absl::nullopt}, false);
    });
  });
  EXPECT_CALL(*mock_lookup_context, getBody(RangeMatcher(0, 5), _))
      .WillOnce([&](const AdjustedByteRange&, LookupBodyCallback&& cb) {
        dispatcher_->post([cb = std::move(cb)]() mutable {
          std::move(cb)(std::make_unique<Buffer::OwnedImpl>("abcde"), false);
        });
      });
  LookupTrailersCallback trailers_callback;
  EXPECT_CALL(*mock_lookup_context, getTrailers(_)).WillOnce([&](LookupTrailersCallback&& cb) {
    trailers_callback = std::move(cb);
  });
  auto filter = makeFilter(mock_http_cache, false);
  EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  filter->onDestroy();
  // onTrailers should do nothing because the filter was destroyed.
  trailers_callback(std::make_unique<Http::TestResponseTrailerMapImpl>());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(CacheFilterTest, BodyReadFromCacheLimitedToBufferSizeChunks) {
  request_headers_.setHost("CacheHitWithBody");
  // Set the buffer limit to 5 bytes, and we will have the file be of size
  // 8 bytes.
  EXPECT_CALL(encoder_callbacks_, encoderBufferLimit()).WillRepeatedly(::testing::Return(5));
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    std::unique_ptr<Http::ResponseHeaderMap> response_headers =
        std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_);
    dispatcher_->post([cb = std::move(cb),
                       response_headers = std::move(response_headers)]() mutable {
      std::move(cb)(
          LookupResult{CacheEntryStatus::Ok, std::move(response_headers), 8, absl::nullopt}, false);
    });
  });
  EXPECT_CALL(*mock_lookup_context, getBody(RangeMatcher(0, 5), _))
      .WillOnce([&](const AdjustedByteRange&, LookupBodyCallback&& cb) {
        dispatcher_->post([cb = std::move(cb)]() mutable {
          std::move(cb)(std::make_unique<Buffer::OwnedImpl>("abcde"), false);
        });
      });
  EXPECT_CALL(*mock_lookup_context, getBody(RangeMatcher(5, 8), _))
      .WillOnce([&](const AdjustedByteRange&, LookupBodyCallback&& cb) {
        dispatcher_->post([cb = std::move(cb)]() mutable {
          std::move(cb)(std::make_unique<Buffer::OwnedImpl>("fgh"), true);
        });
      });

  CacheFilterSharedPtr filter = makeFilter(mock_http_cache, false);

  // The filter should encode cached headers.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false));

  // The filter should encode cached data in two pieces.
  EXPECT_CALL(
      decoder_callbacks_,
      encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq("abcde")), false));
  EXPECT_CALL(decoder_callbacks_,
              encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq("fgh")), true));

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
  pumpDispatcher();

  filter->onDestroy();
  filter.reset();
}

TEST_F(CacheFilterTest, CacheInsertAbortedByCache) {
  request_headers_.setHost("CacheHitWithBody");
  const std::string body = "abc";
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  MockInsertContext* mock_insert_context = mock_http_cache->mockInsertContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    dispatcher_->post([cb = std::move(cb)]() mutable { std::move(cb)(LookupResult{}, false); });
  });
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete, bool) {
        dispatcher_->post([cb = std::move(insert_complete)]() mutable { std::move(cb)(true); });
      });
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, false))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        dispatcher_->post(
            [cb = std::move(ready_for_next_chunk)]() mutable { std::move(cb)(false); });
      });
  {
    // Create filter for request 0.
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache);

    testDecodeRequestMiss(0, filter);

    // Encode response.
    response_headers_.setContentLength(body.size());
    receiveUpstreamHeaders(0, response_headers_, false);
    receiveUpstreamBody(0, body, false);
    EXPECT_CALL(*mock_upstreams_[0], reset());
    pumpDispatcher();

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheMiss));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertAbortedByCache));
  }
}

TEST_F(CacheFilterTest, FilterDestroyedWhileIncompleteCacheWriteInQueueShouldCompleteWrite) {
  request_headers_.setHost("CacheHitWithBody");
  const std::string body = "abc";
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  std::weak_ptr<MockHttpCache> weak_cache_pointer = mock_http_cache;
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  MockInsertContext* mock_insert_context = mock_http_cache->mockInsertContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    dispatcher_->post([cb = std::move(cb)]() mutable { std::move(cb)(LookupResult{}, false); });
  });
  InsertCallback captured_insert_header_callback;
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete,
                    bool) { captured_insert_header_callback = std::move(insert_complete); });
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, false))
      .WillOnce([this](const Buffer::Instance&, InsertCallback insert_complete, bool) {
        dispatcher_->post([cb = std::move(insert_complete)]() mutable { cb(true); });
      });
  EXPECT_CALL(*mock_insert_context, insertTrailers(_, _))
      .WillOnce([this](const Http::ResponseTrailerMap&, InsertCallback insert_complete) {
        dispatcher_->post([cb = std::move(insert_complete)]() mutable { cb(true); });
      });

  {
    // Create filter for request 0 and move the local shared_ptr,
    // transferring ownership to the filter.
    CacheFilterSharedPtr filter = makeFilter(std::move(mock_http_cache));

    testDecodeRequestMiss(0, filter);

    // Encode header of response.
    response_headers_.setContentLength(body.size());
    receiveUpstreamHeaders(0, response_headers_, false);
    // Destroy the filter prematurely (it goes out of scope).
  }
  ASSERT_THAT(captured_insert_header_callback, NotNull());
  EXPECT_THAT(weak_cache_pointer.lock(), NotNull())
      << "cache instance was unexpectedly destroyed when filter was destroyed";
  // The callback should now continue to write the cache entry. Completing the
  // write allows the UpstreamRequest and CacheInsertQueue to complete and self-destruct.
  captured_insert_header_callback(true);
  pumpDispatcher();
  receiveUpstreamBodyAfterFilterDestroyed(0, body, false);
  pumpDispatcher();
  Http::TestResponseTrailerMapImpl trailers{{"somekey", "somevalue"}};
  receiveUpstreamTrailersAfterFilterDestroyed(0, trailers);
  pumpDispatcher();
}

TEST_F(CacheFilterTest, FilterDeletedWhileCompleteCacheWriteInQueueShouldContinueWrite) {
  request_headers_.setHost("CacheHitWithBody");
  const std::string body = "abc";
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  MockInsertContext* mock_insert_context = mock_http_cache->mockInsertContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    dispatcher_->post([cb = std::move(cb)]() mutable { std::move(cb)(LookupResult{}, false); });
  });
  InsertCallback captured_insert_header_callback;
  InsertCallback captured_insert_body_callback;
  EXPECT_CALL(*mock_insert_context, insertHeaders(_, _, _, false))
      .WillOnce([&](const Http::ResponseHeaderMap&, const ResponseMetadata&,
                    InsertCallback insert_complete,
                    bool) { captured_insert_header_callback = std::move(insert_complete); });
  EXPECT_CALL(*mock_insert_context, insertBody(_, _, true))
      .WillOnce([&](const Buffer::Instance&, InsertCallback ready_for_next_chunk, bool) {
        captured_insert_body_callback = std::move(ready_for_next_chunk);
      });
  populateCommonCacheEntry(0, makeFilter(mock_http_cache), body);
  // Header callback should be captured, body callback should not yet since the
  // queue has not reached that chunk.
  ASSERT_THAT(captured_insert_header_callback, NotNull());
  ASSERT_THAT(captured_insert_body_callback, IsNull());
  // The callback should be posted to the dispatcher.
  captured_insert_header_callback(true);
  // Run events on the dispatcher so that the callback is invoked,
  // where it should now proceed to write the body chunk, since the
  // write is still completable.
  pumpDispatcher();
  // So the mock should now be writing the body.
  ASSERT_THAT(captured_insert_body_callback, NotNull());
  captured_insert_body_callback(true);
  // The callback should be posted to the dispatcher.
  // Run events on the dispatcher so that the callback is invoked,
  // where it should now do nothing due to the filter being destroyed.
  pumpDispatcher();
}

TEST_F(CacheFilterTest, SuccessfulValidation) {
  request_headers_.setHost("SuccessfulValidation");
  const std::string body = "abc";
  const std::string etag = "abc123";
  const std::string last_modified_date = formatter_.now(time_source_);

  // Encode response
  // Add Etag & Last-Modified headers to the response for validation
  response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, last_modified_date);
  response_headers_.setContentLength(body.size());
  populateCommonCacheEntry(0, makeFilter(simple_cache_), body);
  waitBeforeSecondRequest();
  {
    // Create filter for request 2
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make request require validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

    // Decoding the request should find a cached response that requires validation.
    // As far as decoding the request is concerned, this is the same as a cache miss with the
    // exception of injecting validation precondition headers.
    testDecodeRequestMiss(1, filter);

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", etag}, {"if-modified-since", last_modified_date}};
    EXPECT_THAT(mock_upstreams_headers_sent_[1],
                testing::Optional(IsSupersetOfHeaders(injected_headers)));

    // Encode 304 response
    // Advance time to make sure the cached date is updated with the 304 date
    const std::string not_modified_date = formatter_.now(time_source_);
    Http::TestResponseHeaderMapImpl not_modified_response_headers = {{":status", "304"},
                                                                     {"date", not_modified_date}};

    // Receiving the 304 response should result in sending the merged headers with
    // updated date.
    Http::TestResponseHeaderMapImpl expected_response_headers = response_headers_;
    expected_response_headers.setDate(not_modified_date);

    // The upstream should be reset on not_modified
    receiveUpstreamHeadersWithReset(1, not_modified_response_headers, true,
                                    IsSupersetOfHeaders(expected_response_headers));

    // It should be impossible for onData to be called on the upstream after reset
    // has been called on it.

    // The filter should add the cached response body to encoded data.
    EXPECT_CALL(
        decoder_callbacks_,
        encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));

    // The cache getBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    pumpDispatcher();

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::StaleHitWithSuccessfulValidation));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::HeaderUpdate));
  }
}

TEST_F(CacheFilterTest, SuccessfulValidationWithFilterDestroyedDuringContinueEncoding) {
  request_headers_.setHost("SuccessfulValidation");
  const std::string body = "abc";
  const std::string etag = "abc123";
  const std::string last_modified_date = formatter_.now(time_source_);
  // Encode response
  // Add Etag & Last-Modified headers to the response for validation
  response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, last_modified_date);
  response_headers_.setContentLength(body.size());
  populateCommonCacheEntry(0, makeFilter(simple_cache_), body);
  waitBeforeSecondRequest();
  {
    // Create filter for request 2
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make request require validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

    // Decoding the request should find a cached response that requires validation.
    // As far as decoding the request is concerned, this is the same as a cache miss with the
    // exception of injecting validation precondition headers.
    testDecodeRequestMiss(1, filter);

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", etag}, {"if-modified-since", last_modified_date}};
    EXPECT_THAT(mock_upstreams_headers_sent_[1],
                testing::Optional(IsSupersetOfHeaders(injected_headers)));

    // Encode 304 response
    // Advance time to make sure the cached date is updated with the 304 date
    const std::string not_modified_date = formatter_.now(time_source_);
    Http::TestResponseHeaderMapImpl not_modified_response_headers = {{":status", "304"},
                                                                     {"date", not_modified_date}};

    // Check for the cached response headers with updated date
    Http::TestResponseHeaderMapImpl expected_response_headers = response_headers_;
    expected_response_headers.setDate(not_modified_date);

    // The upstream should be reset on not_modified
    receiveUpstreamHeadersWithReset(1, not_modified_response_headers, true,
                                    IsSupersetOfHeaders(expected_response_headers));

    // It should be impossible for onBody to be called after reset was called.

    // The filter should add the cached response body to encoded data.
    EXPECT_CALL(
        decoder_callbacks_,
        encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));

    // The cache getBody callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    pumpDispatcher();

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);
  }
}

TEST_F(CacheFilterTest, UnsuccessfulValidation) {
  request_headers_.setHost("UnsuccessfulValidation");
  const std::string body = "abc";
  const std::string etag = "abc123";
  const std::string last_modified_date = formatter_.now(time_source_);
  // Encode response
  // Add Etag & Last-Modified headers to the response for validation.
  response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, last_modified_date);
  response_headers_.setContentLength(body.size());
  populateCommonCacheEntry(0, makeFilter(simple_cache_), body);
  waitBeforeSecondRequest();
  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make request require validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

    // Decoding the request should find a cached response that requires validation.
    // As far as decoding the request is concerned, this is the same as a cache miss with the
    // exception of injecting validation precondition headers.
    testDecodeRequestMiss(1, filter);

    // Make sure validation conditional headers are added.
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", etag}, {"if-modified-since", last_modified_date}};
    EXPECT_THAT(mock_upstreams_headers_sent_[1],
                testing::Optional(IsSupersetOfHeaders(injected_headers)));

    // Encode new response.
    // Change the status code to make sure new headers are served, not the cached ones.
    response_headers_.setStatus(204);

    // The filter should not stop encoding iteration as this is a new response.
    receiveUpstreamHeaders(1, response_headers_, false);
    std::string new_body = "";
    receiveUpstreamBody(1, new_body, true);

    // The response headers should have the new status.
    EXPECT_THAT(response_headers_, HeaderHasValueRef(Http::Headers::get().Status, "204"));

    // The filter should not encode any data.
    EXPECT_CALL(encoder_callbacks_, addEncodedData).Times(0);

    // If a cache getBody callback is made, it should be posted to the dispatcher.
    // Run events on the dispatcher so that any available callbacks are invoked.
    pumpDispatcher();

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::StaleHitWithFailedValidation));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::InsertSucceeded));
  }
}

TEST_F(CacheFilterTest, SingleSatisfiableRange) {
  request_headers_.setHost("SingleSatisfiableRange");
  const std::string body = "abc";
  response_headers_.setContentLength(body.size());
  populateCommonCacheEntry(0, makeFilter(simple_cache_), body);
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
    pumpDispatcher();

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheHit));
    EXPECT_THAT(insertStatus(), IsOkAndHolds(InsertStatus::NoInsertCacheHit));
  }
}

TEST_F(CacheFilterTest, MultipleSatisfiableRanges) {
  request_headers_.setHost("MultipleSatisfiableRanges");
  const std::string body = "abc";
  response_headers_.setContentLength(body.size());
  populateCommonCacheEntry(0, makeFilter(simple_cache_), body);
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
    pumpDispatcher();

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::CacheHit));
  }
}

TEST_F(CacheFilterTest, NotSatisfiableRange) {
  request_headers_.setHost("NotSatisfiableRange");
  const std::string body = "abc";
  response_headers_.setContentLength(body.size());
  populateCommonCacheEntry(0, makeFilter(simple_cache_), body);
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
    pumpDispatcher();

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
  populateCommonCacheEntry(0, makeFilter(simple_cache_));
  {
    // Create filter for request 1.
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
  pumpDispatcher();

  ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
}

TEST_F(CacheFilterTest, LocalReplyDuringLookup) {
  request_headers_.setHost("LocalReplyDuringLookup");
  populateCommonCacheEntry(0, makeFilter(simple_cache_));
  {
    // Create filter for request 1.
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
    pumpDispatcher();

    filter->onStreamComplete();
    EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::RequestIncomplete));
  }
}

// Mark tests with EXPECT_ENVOY_BUG as death tests:
// https://google.github.io/googletest/advanced.html#death-test-naming
using CacheFilterDeathTest = CacheFilterTest;

TEST_F(CacheFilterDeathTest, BadRangeRequestLookup) {
  request_headers_.setHost("BadRangeRequestLookup");
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    dispatcher_->post([cb = std::move(cb), this]() mutable {
      // LookupResult with unknown length and an unsatisfiable RangeDetails is invalid.
      std::move(cb)(
          LookupResult{CacheEntryStatus::Ok,
                       std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_),
                       absl::nullopt,
                       RangeDetails{/*satisfiable_ = */ false, {AdjustedByteRange{0, 5}}}},
          false);
    });
  });
  {
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache);
    // encodeHeaders can be called when ENVOY_BUG doesn't exit.
    response_headers_ = {{":status", "416"}};
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), true))
        .Times(testing::AnyNumber());
    request_headers_.addReference(Http::Headers::get().Range, "bytes=-5");
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    EXPECT_ENVOY_BUG(
        pumpDispatcher(),
        "handleCacheHitWithRangeRequest() should not be called with satisfiable_=false");
  }
}

TEST_F(CacheFilterTest, RangeRequestSatisfiedBeforeLengthKnown) {
  request_headers_.setHost("RangeRequestSatisfiedBeforeLengthKnown");
  std::string body = "abcde";
  auto mock_http_cache = std::make_shared<MockHttpCache>();
  MockLookupContext* mock_lookup_context = mock_http_cache->mockLookupContext();
  EXPECT_CALL(*mock_lookup_context, getHeaders(_)).WillOnce([&](LookupHeadersCallback&& cb) {
    // LookupResult with unknown length and an unsatisfiable RangeDetails is invalid.
    dispatcher_->post([cb = std::move(cb), this]() mutable {
      std::move(cb)(
          LookupResult{CacheEntryStatus::Ok,
                       std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers_),
                       absl::nullopt,
                       RangeDetails{/*satisfiable_ = */ true, {AdjustedByteRange{0, 5}}}},
          false);
    });
  });
  EXPECT_CALL(*mock_lookup_context, getBody(RangeMatcher(0, 5), _))
      .WillOnce([&](AdjustedByteRange, LookupBodyCallback&& cb) {
        dispatcher_->post([cb = std::move(cb), &body]() mutable {
          cb(std::make_unique<Buffer::OwnedImpl>(body), false);
        });
      });
  {
    CacheFilterSharedPtr filter = makeFilter(mock_http_cache);
    response_headers_ = {{":status", "206"}, {"content-range", "bytes 0-4/*"}};
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(IsSupersetOfHeaders(response_headers_), false))
        .Times(testing::AnyNumber());
    request_headers_.addReference(Http::Headers::get().Range, "bytes=-5");
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    pumpDispatcher();
  }
}

TEST_F(CacheFilterDeathTest, StreamTimeoutDuringLookup) {
  request_headers_.setHost("StreamTimeoutDuringLookup");
  populateCommonCacheEntry(0, makeFilter(simple_cache_));
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
        pumpDispatcher();

        filter->onStreamComplete();
        EXPECT_THAT(lookupStatus(), IsOkAndHolds(LookupStatus::RequestIncomplete));
      },
      "Request timed out while cache lookup was outstanding.");

  // Clear out captured lookup lambdas from the dispatcher.
  pumpDispatcher();
}

TEST(LookupStatusDeathTest, ResolveLookupStatusRequireValidationAndInitialIsBug) {
  EXPECT_ENVOY_BUG(
      CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation, FilterState::Initial),
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
  EXPECT_EQ(CacheFilter::resolveLookupStatus(absl::nullopt, FilterState::ServingFromCache),
            LookupStatus::Unknown);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(absl::nullopt, FilterState::Destroyed),
            LookupStatus::Unknown);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation,
                                             FilterState::ValidatingCachedResponse),
            LookupStatus::RequestIncomplete);
  EXPECT_EQ(CacheFilter::resolveLookupStatus(CacheEntryStatus::RequiresValidation,
                                             FilterState::ServingFromCache),
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
  response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                    formatter_.now(time_source_));
  populateCommonCacheEntry(0, makeFilter(simple_cache_));
  {
    // Make request 1 to test for added conditional headers
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(1, filter);

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(mock_upstreams_headers_sent_[1],
                testing::Optional(IsSupersetOfHeaders(injected_headers)));
  }
}

TEST_F(ValidationHeadersTest, EtagOnly) {
  request_headers_.setHost("EtagOnly");
  const std::string etag = "abc123";
  response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
  populateCommonCacheEntry(0, makeFilter(simple_cache_));
  {
    // Make request 1 to test for added conditional headers
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(1, filter);

    // Make sure validation conditional headers are added
    // If-Modified-Since falls back to date
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(mock_upstreams_headers_sent_[1],
                testing::Optional(IsSupersetOfHeaders(injected_headers)));
  }
}

TEST_F(ValidationHeadersTest, LastModifiedOnly) {
  request_headers_.setHost("LastModifiedOnly");
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                    formatter_.now(time_source_));
  populateCommonCacheEntry(0, makeFilter(simple_cache_));
  {
    // Make request 2 to test for added conditional headers
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(1, filter);

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(mock_upstreams_headers_sent_[1],
                testing::Optional(IsSupersetOfHeaders(injected_headers)));
  }
}

TEST_F(ValidationHeadersTest, NoEtagOrLastModified) {
  request_headers_.setHost("NoEtagOrLastModified");
  populateCommonCacheEntry(0, makeFilter(simple_cache_));
  {
    // Make request 2 to test for added conditional headers
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(1, filter);

    // Make sure validation conditional headers are added
    // If-Modified-Since falls back to date
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(mock_upstreams_headers_sent_[1],
                testing::Optional(IsSupersetOfHeaders(injected_headers)));
  }
}

TEST_F(ValidationHeadersTest, InvalidLastModified) {
  request_headers_.setHost("InvalidLastModified");
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, "invalid-date");
  populateCommonCacheEntry(0, makeFilter(simple_cache_));
  {
    // Make request 1 to test for added conditional headers
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    testDecodeRequestMiss(1, filter);

    // Make sure validation conditional headers are added
    // If-Modified-Since falls back to date
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(mock_upstreams_headers_sent_[1],
                testing::Optional(IsSupersetOfHeaders(injected_headers)));
  }
}

TEST_F(CacheFilterTest, NoRouteShouldLocalReply) {
  request_headers_.setHost("NoRoute");
  EXPECT_CALL(decoder_callbacks_, route()).WillOnce(Return(nullptr));
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);
    // The filter should stop decoding iteration when decodeHeaders is called as a cache lookup is
    // in progress.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Http::Code::NotFound, _, _, _, "cache_no_route"));
    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    pumpDispatcher();
  }
}

TEST_F(CacheFilterTest, NoClusterShouldLocalReply) {
  request_headers_.setHost("NoCluster");
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(nullptr));
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);
    // The filter should stop decoding iteration when decodeHeaders is called as a cache lookup is
    // in progress.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, "cache_no_cluster"));
    // The cache lookup callback should be posted to the dispatcher.
    // Run events on the dispatcher so that the callback is invoked.
    pumpDispatcher();
  }
}

TEST_F(CacheFilterTest, UpstreamResetMidResponseShouldLocalReply) {
  request_headers_.setHost("UpstreamResetMidResponse");
  {
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);
    testDecodeRequestMiss(0, filter);
    receiveUpstreamHeaders(0, response_headers_, false);
    pumpDispatcher();
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, "cache_upstream_reset"));
    mock_upstreams_callbacks_[0].get().onReset();
    pumpDispatcher();
  }
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

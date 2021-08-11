#include "envoy/event/dispatcher.h"

#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache/cache_filter.h"
#include "source/extensions/filters/http/cache/simple_http_cache/simple_http_cache.h"

#include "test/extensions/filters/http/cache/common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class CacheFilterTest : public ::testing::Test {
protected:
  // The filter has to be created as a shared_ptr to enable shared_from_this() which is used in the
  // cache callbacks.
  CacheFilterSharedPtr makeFilter(HttpCache& cache) {
    auto filter = std::make_shared<CacheFilter>(config_, /*stats_prefix=*/"", context_.scope(),
                                                context_.timeSource(), cache);
    filter->setDecoderFilterCallbacks(decoder_callbacks_);
    filter->setEncoderFilterCallbacks(encoder_callbacks_);
    return filter;
  }

  void SetUp() override {
    ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(::testing::ReturnRef(*dispatcher_));
    // Initialize the time source (otherwise it returns the real time)
    time_source_.setSystemTime(std::chrono::hours(1));
    // Use the initialized time source to set the response date header
    response_headers_.setDate(formatter_.now(time_source_));
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

  SimpleHttpCache simple_cache_;
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config_;
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
    filter->onDestroy();
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
    filter->onDestroy();
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
    filter->onDestroy();
  }
}

TEST_F(CacheFilterTest, CacheHitNoBody) {
  request_headers_.setHost("CacheHitNoBody");

  {
    // Create filter for request 1.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestMiss(filter);

    // Encode response headers.
    EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter->onDestroy();
  }
  waitBeforeSecondRequest();
  {
    // Create filter for request 2.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestHitNoBody(filter);

    filter->onDestroy();
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

    filter->onDestroy();
  }
  waitBeforeSecondRequest();
  {
    // Create filter for request 2
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    testDecodeRequestHitWithBody(filter, body);

    filter->onDestroy();
  }
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
    filter->onDestroy();
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

    filter->onDestroy();
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
    filter->onDestroy();
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
    response_headers_.setStatus(201);

    // The filter should not stop encoding iteration as this is a new response.
    EXPECT_EQ(filter->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    Buffer::OwnedImpl new_body;
    EXPECT_EQ(filter->encodeData(new_body, true), Http::FilterDataStatus::Continue);

    // The response headers should have the new status.
    EXPECT_THAT(response_headers_, HeaderHasValueRef(Http::Headers::get().Status, "201"));

    // The filter should not encode any data.
    EXPECT_CALL(encoder_callbacks_, addEncodedData).Times(0);

    // If a cache getBody callback is made, it should be posted to the dispatcher.
    // Run events on the dispatcher so that any available callbacks are invoked.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);

    filter->onDestroy();
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
    filter->onDestroy();
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
    filter->onDestroy();
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
    filter->onDestroy();
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
    filter->onDestroy();
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
    filter->onDestroy();
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

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter->onDestroy();
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
    filter->onDestroy();
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
    filter->onDestroy();
  }
  {
    // Create filter for request 2.
    CacheFilterSharedPtr filter = makeFilter(simple_cache_);

    // Call decode headers to start the cache lookup, which should immediately post the callback to
    // the dispatcher.
    EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Destroy the filter
    filter->onDestroy();
    filter.reset();

    // Make sure that onHeaders was not called by making sure no decoder callbacks were made.
    EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);

    // Run events on the dispatcher so that the callback is invoked after the filter deletion.
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
  }
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

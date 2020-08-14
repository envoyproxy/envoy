#include "envoy/http/header_map.h"

#include "common/http/headers.h"

#include "extensions/filters/http/cache/cache_filter.h"
#include "extensions/filters/http/cache/simple_http_cache/simple_http_cache.h"

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
  CacheFilter makeFilter(HttpCache& cache) {
    CacheFilter filter(config_, /*stats_prefix=*/"", context_.scope(), context_.timeSource(),
                       cache);
    filter.setDecoderFilterCallbacks(decoder_callbacks_);
    filter.setEncoderFilterCallbacks(encoder_callbacks_);
    return filter;
  }

  SimpleHttpCache simple_cache_;
  DelayedCache delayed_cache_;
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Event::SimulatedTimeSystem time_source_;
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-forwarded-proto", "https"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"date", formatter_.now(time_source_)},
                                                    {"cache-control", "public,max-age=3600"}};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(CacheFilterTest, UncacheableRequest) {
  request_headers_.setHost("UncacheableRequest");

  // POST requests are uncacheable
  request_headers_.setMethod(Http::Headers::get().MethodValues.Post);

  for (int i = 0; i < 2; i++) {
    // Create filter for request i
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request i header
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // In the first request was cached the second request would return StopAllIterationAndWatermark
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header
    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, UncacheableResponse) {
  request_headers_.setHost("UncacheableResponse");

  // Responses with "Cache-Control: no-store" are uncacheable
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-store");

  for (int i = 0; i < 2; i++) {
    // Create filter for request i
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request i header
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // In the first request was cached the second request would return StopAllIterationAndWatermark
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header
    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, ImmediateMiss) {
  for (int request = 1; request <= 2; request++) {
    // Each iteration a request is sent to a different host, therefore the second one is a miss
    request_headers_.setHost("ImmediateMiss" + std::to_string(request));

    // Create filter for request 1
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 1 header
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header
    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, DelayedMiss) {
  for (int request = 1; request <= 2; request++) {
    // Each iteration a request is sent to a different host, therefore the second one is a miss
    request_headers_.setHost("DelayedMiss" + std::to_string(request));

    // Create filter for request 1
    CacheFilter filter = makeFilter(delayed_cache_);

    // Decode request 1 header
    // No hit will be found, so the filter should call continueDecoding
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // The filter should stop iteration waiting for cache response
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Make the delayed callback to call onHeaders
    delayed_cache_.delayed_headers_cb_();

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header
    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, ImmediateHitNoBody) {
  request_headers_.setHost("ImmediateHitNoBody");

  {
    // Create filter for request 1.
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 1 header
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header.
    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2.
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 2 header.
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                              HeaderHasValueRef(Http::Headers::get().Age, "0")),
                               true));
    // Make sure the filter did not encode any data
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // Make sure decoding does not continue
    EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, DelayedHitNoBody) {
  request_headers_.setHost("DelayedHitNoBody");

  {
    // Create filter for request 1.
    CacheFilter filter = makeFilter(delayed_cache_);

    // Decode request 1 header
    // No hit will be found, so the filter should call continueDecoding
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // The filter should stop iteration waiting for cache response
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Make the delayed callback to call onHeaders
    delayed_cache_.delayed_headers_cb_();

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header.
    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2.
    CacheFilter filter = makeFilter(delayed_cache_);

    // Decode request 2 header
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                              HeaderHasValueRef(Http::Headers::get().Age, "0")),
                               true));
    // Make sure the filter did not encode any data
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // Make sure decoding does not continue
    EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    delayed_cache_.delayed_headers_cb_();
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, ImmediateHitBody) {
  request_headers_.setHost("ImmediateHitBody");
  const std::string body = "abc";

  {
    // Create filter for request 1.
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 1 header
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);

    // Encode response header.
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2.
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 2 header
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                              HeaderHasValueRef(Http::Headers::get().Age, "0")),
                               false));
    EXPECT_CALL(
        decoder_callbacks_,
        encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));
    // Make sure decoding does not continue
    EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, DelayedHitBody) {
  request_headers_.setHost("DelayedHitBody");
  const std::string body = "abc";

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter(delayed_cache_);

    // Decode request 1 header
    // No hit will be found, so the filter should call continueDecoding
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // The filter should stop iteration waiting for cache response
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Make the delayed callback to call onHeaders
    delayed_cache_.delayed_headers_cb_();

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter(delayed_cache_);

    // Decode request 2 header

    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                              HeaderHasValueRef(Http::Headers::get().Age, "0")),
                               false));
    EXPECT_CALL(
        decoder_callbacks_,
        encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));

    // Make sure decoding does not continue
    EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Make the delayed callbacks to call onHeaders & onBody
    delayed_cache_.delayed_headers_cb_();
    delayed_cache_.delayed_body_cb_();
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, ImmediateSuccessfulValidation) {
  request_headers_.setHost("ImmediateSuccessfulValidation");
  const std::string body = "abc";

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 1 header
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);

    // Encode response

    // Add Etag & Last-Modified headers to the response for validation
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, "abc123");
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                      formatter_.now(time_source_));

    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter(simple_cache_);

    // Make request require validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

    // Decode request 2 header
    // Make sure the filter did not encode any headers or data (during decoding)
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode 304 response
    // Advance time to make sure the cached date is updated with the 304 date
    time_source_.advanceTimeWait(std::chrono::seconds(10));
    const std::string not_modified_date = formatter_.now(time_source_);
    Http::TestResponseHeaderMapImpl not_modified_response_headers = {{":status", "304"},
                                                                     {"date", not_modified_date}};

    // Check that the cached response body is encoded
    Buffer::OwnedImpl buffer(body);
    EXPECT_CALL(
        encoder_callbacks_,
        addEncodedData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));

    // The cache is immediate so everything should be done before encodeHeaders returns
    EXPECT_EQ(filter.encodeHeaders(not_modified_response_headers, true),
              Http::FilterHeadersStatus::Continue);

    // Check for the cached response headers with updated date
    Http::TestResponseHeaderMapImpl updated_response_headers = response_headers_;
    updated_response_headers.setDate(not_modified_date);
    EXPECT_THAT(not_modified_response_headers,
                testing::AllOf(IsSupersetOfHeaders(updated_response_headers),
                               HeaderHasValueRef(Http::Headers::get().Age, "0")));

    // The cache is immediate so everything should be done before CacheFilter::encodeData
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::Continue);

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);

    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, DelayedSuccessfulValidation) {
  request_headers_.setHost("DelayedSuccessfulValidation");
  const std::string body = "abc";

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter(delayed_cache_);

    // Decode request 1 header
    // No hit will be found, so the filter should call continueDecoding
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // The filter should stop iteration waiting for cache response
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Make the delayed callback to call onHeaders
    delayed_cache_.delayed_headers_cb_();

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response

    // Add Etag & Last-Modified headers to the response for validation
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, "abc123");
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                      formatter_.now(time_source_));

    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter(delayed_cache_);

    // Make request require validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

    // Decode request 2 header
    // No hit will be found, so the filter should call continueDecoding
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // The filter should stop iteration waiting for cache response
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Make the delayed callback to call onHeaders
    delayed_cache_.delayed_headers_cb_();

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode 304 response
    // Advance time to make sure the cached date is updated with the 304 date
    time_source_.advanceTimeWait(std::chrono::seconds(10));
    const std::string not_modified_date = formatter_.now(time_source_);
    Http::TestResponseHeaderMapImpl not_modified_response_headers = {{":status", "304"},
                                                                     {"date", not_modified_date}};

    // Check that the cached response body is encoded
    Buffer::OwnedImpl buffer(body);
    EXPECT_CALL(
        encoder_callbacks_,
        addEncodedData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));

    // The cache is delayed so encoding iteration should be stopped until the body is encoded
    // StopIteration does not stop encodeData of the same filter from being called
    EXPECT_EQ(filter.encodeHeaders(not_modified_response_headers, true),
              Http::FilterHeadersStatus::StopIteration);

    // Check for the cached response headers with updated date
    Http::TestResponseHeaderMapImpl updated_response_headers = response_headers_;
    updated_response_headers.setDate(not_modified_date);
    EXPECT_THAT(not_modified_response_headers,
                testing::AllOf(IsSupersetOfHeaders(updated_response_headers),
                               HeaderHasValueRef(Http::Headers::get().Age, "0")));

    // A 304 response should not have a body, so encodeData should not be called
    // However, if a body is present by mistake, encodeData should stop iteration until
    // encoding the cached response is done
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::StopIterationAndBuffer);

    // Delayed call to onBody to encode cached response
    delayed_cache_.delayed_body_cb_();

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);

    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, ImmediateUnsuccessfulValidation) {
  request_headers_.setHost("ImmediateUnsuccessfulValidation");

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 1 header
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);

    // Encode response

    // Add Etag & Last-Modified headers to the response for validation
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, "abc123");
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                      formatter_.now(time_source_));

    const std::string body = "abc";
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter(simple_cache_);

    // Make request require validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

    // Decode request 2 header
    // Make sure the filter did not encode any headers or data (during decoding)
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode new response
    // Change the status code to make sure new headers are served, not the cached ones
    response_headers_.setStatus(201);

    // Check that no cached data is encoded
    EXPECT_CALL(encoder_callbacks_, addEncodedData).Times(0);

    // The cache is immediate so everything should be done before encodeHeaders returns
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);

    // Check for the cached response headers with updated date
    EXPECT_THAT(response_headers_, HeaderHasValueRef(Http::Headers::get().Status, "201"));

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);

    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, DelayedUnuccessfulValidation) {
  request_headers_.setHost("DelayedUnuccessfulValidation");

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter(delayed_cache_);

    // Decode request 1 header
    // No hit will be found, so the filter should call continueDecoding
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // The filter should stop iteration waiting for cache response
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Make the delayed callback to call onHeaders
    delayed_cache_.delayed_headers_cb_();

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response

    // Add Etag & Last-Modified headers to the response for validation
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, "abc123");
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                      formatter_.now(time_source_));

    const std::string body = "abc";
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter(delayed_cache_);

    // Make request require validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

    // Decode request 2 header
    // No hit will be found, so the filter should call continueDecoding
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    // Make sure the filter did not encode any headers or data
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    // The filter should stop iteration waiting for cache response
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);

    // Make the delayed callback to call onHeaders
    delayed_cache_.delayed_headers_cb_();

    // Make sure validation conditional headers are added
    const Http::TestRequestHeaderMapImpl injected_headers = {
        {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
    EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));

    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode new response
    // Change the status code to make sure new headers are served, not the cached ones
    response_headers_.setStatus(201);

    // Check that no cached response body is encoded
    EXPECT_CALL(encoder_callbacks_, addEncodedData).Times(0);

    // The delayed cache has no effect here as nothing is fetched from cache
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);

    // Check for the cached response headers with updated date
    EXPECT_THAT(response_headers_, HeaderHasValueRef(Http::Headers::get().Status, "201"));

    ::testing::Mock::VerifyAndClearExpectations(&encoder_callbacks_);

    filter.onDestroy();
  }
}

// Send two identical GET requests with bodies. The CacheFilter will just pass everything through.
TEST_F(CacheFilterTest, GetRequestWithBodyAndTrailers) {
  request_headers_.setHost("GetRequestWithBodyAndTrailers");
  const std::string body = "abc";
  Buffer::OwnedImpl request_buffer(body);
  Http::TestRequestTrailerMapImpl request_trailers;

  for (int i = 0; i < 2; ++i) {
    CacheFilter filter = makeFilter(simple_cache_);

    EXPECT_EQ(filter.decodeHeaders(request_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter.decodeData(request_buffer, false), Http::FilterDataStatus::Continue);
    EXPECT_EQ(filter.decodeTrailers(request_trailers), Http::FilterTrailersStatus::Continue);

    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
}

// A new type alias for a different type of tests that use the exact same class
using ValidationHeadersTest = CacheFilterTest;

TEST_F(ValidationHeadersTest, EtagAndLastModified) {
  request_headers_.setHost("EtagAndLastModified");
  const std::string etag = "abc123";

  // Make request 1 to insert the response into cache
  {
    CacheFilter filter = makeFilter(simple_cache_);
    filter.decodeHeaders(request_headers_, true);

    // Add validation headers to the response
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                      formatter_.now(time_source_));

    filter.encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilter filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    filter.decodeHeaders(request_headers_, true);

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
    CacheFilter filter = makeFilter(simple_cache_);
    filter.decodeHeaders(request_headers_, true);

    // Add validation headers to the response
    response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);

    filter.encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilter filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    filter.decodeHeaders(request_headers_, true);

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
    CacheFilter filter = makeFilter(simple_cache_);
    filter.decodeHeaders(request_headers_, true);

    // Add validation headers to the response
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                      formatter_.now(time_source_));

    filter.encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilter filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    filter.decodeHeaders(request_headers_, true);

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
    CacheFilter filter = makeFilter(simple_cache_);
    filter.decodeHeaders(request_headers_, true);
    filter.encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilter filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    filter.decodeHeaders(request_headers_, true);

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
    CacheFilter filter = makeFilter(simple_cache_);
    filter.decodeHeaders(request_headers_, true);

    // Add validation headers to the response
    response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, "invalid-date");

    filter.encodeHeaders(response_headers_, true);
  }
  // Make request 2 to test for added conditional headers
  {
    CacheFilter filter = makeFilter(simple_cache_);

    // Make sure the request requires validation
    request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
    filter.decodeHeaders(request_headers_, true);

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

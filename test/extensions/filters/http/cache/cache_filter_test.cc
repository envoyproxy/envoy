#include "envoy/event/dispatcher.h"

#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache/cache_filter.h"
#include "source/extensions/filters/http/cache/simple_http_cache/simple_http_cache.h"

#include "test/extensions/filters/http/cache/common.h"
#include "test/extensions/filters/http/cache/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::AllOf;
using ::testing::ByMove;
using ::testing::Field;
using ::testing::InvokeArgument;
using ::testing::InvokeWithoutArgs;
using ::testing::Ref;
using ::testing::ReturnRef;
using ::testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

constexpr absl::string_view kBody = "abc";

// Returns a std::function that can be used as an action with
// MockLookupContext::getBody to return the provided body or chunk.
std::function<void(const AdjustedByteRange&, LookupBodyCallback&&)>
getBodyChunk(absl::string_view chunk) {
  return [chunk = std::string(chunk)](const AdjustedByteRange&, LookupBodyCallback&& cb) {
    cb(std::make_unique<Envoy::Buffer::OwnedImpl>(chunk));
  };
}

MATCHER_P(bufferContains, matcher,
          absl::StrCat(negation ? "isn't" : "is", " a buffer with contents that ",
                       testing::DescribeMatcher<std::string>(matcher))) {
  *result_listener << "which contains: " << arg.toString();
  return testing::ExplainMatchResult(matcher, arg.toString(), result_listener);
}

MATCHER_P(doesNotHaveHeaders, matcher, "") {
  for (const std::string& header : matcher) {
    if (!arg.get(Envoy::Http::LowerCaseString(header)).empty()) {
      return false;
    }
  }
  return true;
}

class CacheFilterTest : public ::testing::Test {
protected:
  void SetUp() override {
    cache_ = std::make_shared<StrictMock<MockHttpCache>>();
    filter_ = makeFilter();
    ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(ReturnRef(*dispatcher_));
    ON_CALL(decoder_callbacks_.stream_info_, filterState()).WillByDefault(ReturnRef(filter_state_));
    // Expect no calls to encodeHeaders, encodeData, or continueDecoding; except
    // as tests specify.
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_).Times(0);
    EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);
    EXPECT_CALL(decoder_callbacks_, continueDecoding).Times(0);
    // Initialize the time source (otherwise it returns the real time)
    time_source_.setSystemTime(std::chrono::hours(1));
    // Use the initialized time source to set the response date header
    response_headers_.setDate(formatter_.now(time_source_));
  }

  void TearDown() override { filter_->onDestroy(); }

  // The filter has to be created as a shared_ptr to enable shared_from_this() which is used in the
  // cache callbacks.
  CacheFilterSharedPtr makeFilter() {
    auto filter = std::make_shared<CacheFilter>(config_, /*stats_prefix=*/"", context_.scope(),
                                                context_.timeSource(), *cache_);
    filter->setDecoderFilterCallbacks(decoder_callbacks_);
    filter->setEncoderFilterCallbacks(encoder_callbacks_);
    return filter;
  }

  // Set expectations on cache_ and decoder_callbacks_ for a cache miss. If
  // context_callback is provided, it's called on the MockLookupContext during
  // the makeLookupContext() call.
  void expectCacheMiss(std::function<void(MockLookupContext&)> context_callback = nullptr) {
    EXPECT_CALL(*cache_, makeLookupContext).WillOnce(InvokeWithoutArgs([this, context_callback]() {
      std::unique_ptr<MockLookupContext> lookup_context =
          makeLookupContext({/*cache_entry_status_=*/CacheEntryStatus::Unusable});
      if (context_callback) {
        context_callback(*lookup_context);
      }
      return lookup_context;
    }));
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
  }

  void expectLookup(LookupResult lookup_result,
                    std::function<void(MockLookupContext&)> context_callback = nullptr) {
    // Wrap the move-only LookupResult in a copyable shared_ptr to fit with
    // EXPECT_CALL(...).WillOnce()'s interface.
    auto result_wrapper = std::make_shared<LookupResult>(std::move(lookup_result));
    EXPECT_CALL(*cache_, makeLookupContext)
        .WillOnce(InvokeWithoutArgs([this, result_wrapper, context_callback]() {
          std::unique_ptr<MockLookupContext> lookup_context =
              makeLookupContext(std::move(*result_wrapper));
          if (context_callback) {
            std::move(context_callback)(*lookup_context);
          }
          return lookup_context;
        }));
  }

  void expectCacheHitNoBody() {
    // The filter should ask the cache for a lookup context, which will return a
    // headers-only hit.
    expectLookup({/*cache_entry_status_=*/CacheEntryStatus::Ok,
                  /*headers_=*/nullptr, /*content_length_=*/0});

    // The filter should encode cached headers.
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(isSupersetOfResponseHeadersWithAge(), true))
        .WillOnce(Invoke(filter_.get(), &CacheFilter::encodeHeaders));
  }

  void expectCacheHitWithBody(absl::string_view body) {
    expectLookup({/*cache_entry_status_=*/CacheEntryStatus::Ok,
                  /*headers_=*/nullptr,
                  /*content_length_=*/body.size()},
                 [body = std::string(body)](MockLookupContext& lookup) {
                   EXPECT_CALL(lookup, getBody(AdjustedByteRange(0, body.size()), _))
                       .WillOnce(getBodyChunk(body));
                 });

    // The filter should encode cached headers.
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(isSupersetOfResponseHeadersWithAge(), false))
        .WillOnce(Invoke(filter_.get(), &CacheFilter::encodeHeaders));

    // The filter should encode cached data.
    EXPECT_CALL(decoder_callbacks_, encodeData(bufferContains(body), true));
  }

  std::unique_ptr<MockLookupContext> makeLookupContext() {
    auto lookup_context = std::make_unique<StrictMock<MockLookupContext>>();
    EXPECT_CALL(*lookup_context, onDestroy());

    return lookup_context;
  }

  std::unique_ptr<MockLookupContext> makeLookupContext(LookupResult result) {
    std::unique_ptr<MockLookupContext> lookup_context = makeLookupContext();
    EXPECT_CALL(*lookup_context, getHeaders(_)).WillOnce(getHeaders(std::move(result)));
    return lookup_context;
  }

  // Returns a std::function that can be used as an action with
  // MockLookupContext::getHeaders to find response_headers_, with other fields
  // of the LookupResult as specified.
  std::function<void(LookupHeadersCallback)> getHeaders(LookupResult lookup_result) {
    // gmock actions don't work with closures over move-only types, so copy over
    // every member of the lookup result individually.
    return [response_headers = response_headers_, age = age_,
            cache_entry_status = lookup_result.cache_entry_status_,
            content_length = lookup_result.content_length_,
            range_details = lookup_result.range_details_,
            has_trailers = lookup_result.has_trailers_](LookupHeadersCallback cb) {
      auto response_headers_from_cache =
          std::make_unique<Envoy::Http::TestResponseHeaderMapImpl>(response_headers);
      response_headers_from_cache->setCopy(Http::CustomHeaders::get().Age, age);
      cb({/*cache_entry_status_=*/cache_entry_status,
          /*headers_=*/std::move(response_headers_from_cache),
          /*content_length_=*/content_length,
          /*range_details_=*/range_details,
          /*has_trailers_=*/has_trailers});
    };
  }

  // Returns a unique_ptr to a MockInsertContext for a headers-only insert.
  std::unique_ptr<MockInsertContext> headerOnlyInsertContext() const {
    auto insert_context = std::make_unique<StrictMock<MockInsertContext>>();
    EXPECT_CALL(*insert_context, insertHeaders(isSupersetOfCacheableResponseHeaders(), _, true));
    EXPECT_CALL(*insert_context, onDestroy());
    return std::move(insert_context);
  }

  // Returns a unique_ptr to a MockInsertContext for an insert with body.
  std::unique_ptr<MockInsertContext>
  bodyInsertContext(const std::vector<absl::string_view>& body_chunks) const {
    testing::InSequence seq;
    auto insert_context = std::make_unique<StrictMock<MockInsertContext>>();
    int content_length = 0;
    for (absl::string_view chunk : body_chunks) {
      content_length += chunk.length();
    }
    EXPECT_CALL(
        *insert_context,
        insertHeaders(/*response_headers=*/testing::AllOf(
                          isSupersetOfCacheableResponseHeaders(),
                          Envoy::HeaderHasValueRef(Envoy::Http::Headers::get().ContentLength,
                                                   absl::StrCat(content_length))),
                      /*metadata=*/_, /*end_stream=*/false));
    for (const absl::string_view& chunk : body_chunks) {
      bool end_stream = (&chunk == &body_chunks.back());
      EXPECT_CALL(*insert_context, insertBody(bufferContains(chunk), _, end_stream))
          .WillOnce(InvokeArgument<1>(true));
    }
    EXPECT_CALL(*insert_context, onDestroy());

    return std::move(insert_context);
  }

  // Passes request_headers_ into the filter's decodeHeaders(), and runs any
  // subsequent callbacks posted to the dispatcher. Returns the return value of
  // the encodeHeaders() call.
  Envoy::Http::FilterHeadersStatus decodeHeadersAndRunDispatcher(bool end_stream = true) {
    Envoy::Http::FilterHeadersStatus decode_headers_result =
        filter_->decodeHeaders(request_headers_, end_stream);
    dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
    return decode_headers_result;
  }

  void waitBeforeLookup() { time_source_.advanceTimeWait(delay_); }

  testing::Matcher<const Envoy::Http::ResponseHeaderMap&>
  isSupersetOfCacheableHeaders(const Envoy::Http::TestResponseHeaderMapImpl& headers) const {
    Envoy::Http::TestResponseHeaderMapImpl cacheable_response_headers = headers;
    for (const std::string& header : uncached_headers_) {
      cacheable_response_headers.remove(Envoy::Http::LowerCaseString(header));
    }
    return testing::AllOf(IsSupersetOfHeaders(cacheable_response_headers),
                          doesNotHaveHeaders(uncached_headers_));
  }

  testing::Matcher<const Envoy::Http::ResponseHeaderMap&>
  isSupersetOfCacheableResponseHeaders() const {
    return isSupersetOfCacheableHeaders(response_headers_);
  }

  testing::Matcher<Envoy::Http::ResponseHeaderMap&> isSupersetOfResponseHeadersWithAge() {
    return testing::AllOf(IsSupersetOfHeaders(response_headers_),
                          HeaderHasValueRef(Http::CustomHeaders::get().Age, age_));
  }

  std::shared_ptr<StrictMock<MockHttpCache>> cache_;
  envoy::extensions::filters::http::cache::v3::CacheConfig config_;
  std::shared_ptr<CacheFilter> filter_;
  std::shared_ptr<StreamInfo::FilterState> filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Event::SimulatedTimeSystem time_source_;
  std::vector<std::string> uncached_headers_{"x-uncacheable-header"};
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
  const std::string age_ = std::to_string(delay_.count());
};

TEST_F(CacheFilterTest, FilterIsBeingDestroyed) {
  CacheFilterSharedPtr filter = makeFilter();
  filter->onDestroy();
  // decodeHeaders should do nothing... at least make sure it doesn't crash.
  filter->decodeHeaders(request_headers_, true);
}

TEST_F(CacheFilterTest, UncacheableRequest) {
  request_headers_.setHost("UncacheableRequest");

  // POST requests are uncacheable
  request_headers_.setMethod(Http::Headers::get().MethodValues.Post);

  // Uncacheable requests should bypass the cache filter; no cache lookups
  // should be initiated.
  EXPECT_CALL(*cache_, makeLookupContext).Times(0);
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, true),
            Envoy::Http::FilterHeadersStatus::Continue);

  // Encode response header
  EXPECT_CALL(*cache_, makeInsertContext).Times(0);
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true),
            Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(CacheFilterTest, UncacheableResponse) {
  request_headers_.setHost("UncacheableResponse");

  // Responses with "Cache-Control: no-store" are uncacheable
  response_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-store");

  expectCacheMiss();

  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream=*/true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Encode response headers.
  EXPECT_CALL(*cache_, makeInsertContext).Times(0);
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true),
            Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(CacheFilterTest, CacheMiss) {
  request_headers_.setHost("CacheMiss");

  expectCacheMiss();
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream=*/true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  EXPECT_CALL(*cache_, makeInsertContext).WillOnce(Return(ByMove(headerOnlyInsertContext())));

  // Encode response header
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
}

TEST_F(CacheFilterTest, CacheHitNoBody) {
  request_headers_.setHost("CacheHitNoBody");
  waitBeforeLookup();

  expectCacheHitNoBody();
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream=*/true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);
}

TEST_F(CacheFilterTest, CacheHitWithBody) {
  request_headers_.setHost("CacheHitWithBody");
  waitBeforeLookup();

  expectCacheHitWithBody(kBody);
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream*/ true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);
}

TEST_F(CacheFilterTest, SuccessfulValidation) {
  request_headers_.setHost("SuccessfulValidation");
  const std::string etag = "abc123";
  const std::string last_modified_date = formatter_.now(time_source_);
  // Add Etag & Last-Modified headers to the response for validation
  response_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().Etag, etag);
  response_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().LastModified,
                                    last_modified_date);
  response_headers_.setContentLength(kBody.size());
  waitBeforeLookup();

  // Make request require validation
  request_headers_.setReferenceKey(Envoy::Http::CustomHeaders::get().CacheControl, "no-cache");

  const std::string not_modified_date = formatter_.now(time_source_);

  // Set the cache to return a response that requires validation. When the
  // filter sees a 304 response, it should update the cached headers to
  // match the response, and then fetch the cached body.

  Envoy::Http::TestResponseHeaderMapImpl updated_response_headers = response_headers_;
  updated_response_headers.setDate(not_modified_date);

  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::RequiresValidation,
                /*headers_=*/nullptr,
                /*content_length_=*/kBody.size()},
               [this, updated_response_headers](MockLookupContext& lookup_context) {
                 EXPECT_CALL(*cache_, updateHeaders(
                                          Ref(lookup_context),
                                          isSupersetOfCacheableHeaders(updated_response_headers),
                                          Field("response_time_", &ResponseMetadata::response_time_,
                                                time_source_.systemTime())));

                 EXPECT_CALL(lookup_context, getBody(AdjustedByteRange(0, kBody.size()), _))
                     .WillOnce(getBodyChunk(kBody));
               });

  // Decoding the request should find a cached response that requires
  // validation. As far as decoding the request is concerned, this is the same
  // as a cache miss with the exception of injecting validation precondition
  // headers.
  EXPECT_CALL(decoder_callbacks_, continueDecoding);
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream*/ true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Make sure validation conditional headers are added
  const Envoy::Http::TestRequestHeaderMapImpl injected_headers = {
      {"if-none-match", etag}, {"if-modified-since", last_modified_date}};
  EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));

  // Encode 304 response
  Envoy::Http::TestResponseHeaderMapImpl not_modified_response_headers = {
      {":status", "304"}, {"date", not_modified_date}};

  // The filter should stop encoding iteration when encodeHeaders is called as a cached response
  // is being fetched and added to the encoding stream. StopIteration does not stop encodeData of
  // the same filter from being called
  EXPECT_EQ(filter_->encodeHeaders(not_modified_response_headers, true),
            Http::FilterHeadersStatus::StopIteration);

  // Check for the cached response headers with updated date
  EXPECT_THAT(not_modified_response_headers, IsSupersetOfHeaders(updated_response_headers));

  // The filter should add the cached response body to encoded data.
  Buffer::OwnedImpl buffer(kBody);
  EXPECT_CALL(encoder_callbacks_, addEncodedData(bufferContains(kBody), true));

  // The cache getBody callback should be posted to the dispatcher.
  // Run events on the dispatcher so that the callback is invoked.
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
}

TEST_F(CacheFilterTest, UnsuccessfulValidation) {
  request_headers_.setHost("UnsuccessfulValidation");
  const std::string etag = "abc123";
  const std::string last_modified_date = formatter_.now(time_source_);
  response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, last_modified_date);
  response_headers_.setContentLength(kBody.size());

  waitBeforeLookup();

  // Make request require validation
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

  // Decoding the request should find a cached response that requires
  // validation. As far as decoding the request is concerned, this is the same
  // as a cache miss with the exception of injecting validation precondition
  // headers.
  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::RequiresValidation,
                /*headers_=*/nullptr,
                /*content_length_=*/kBody.size()});
  EXPECT_CALL(decoder_callbacks_, continueDecoding);
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream=*/true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Make sure validation conditional headers are added.
  const Envoy::Http::TestRequestHeaderMapImpl injected_headers = {
      {"if-none-match", etag}, {"if-modified-since", last_modified_date}};
  EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));

  // Change the status code to make sure new headers are served, not the
  // cached ones.
  response_headers_.setStatus(204);
  std::string new_body = "";
  response_headers_.setContentLength(new_body.size());

  EXPECT_CALL(*cache_, makeInsertContext).WillOnce(Return(ByMove(bodyInsertContext({new_body}))));

  // Encode new response.
  // The filter should not stop encoding iteration as this is a new response.
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
  Envoy::Buffer::OwnedImpl new_body_buffer(new_body);
  EXPECT_EQ(filter_->encodeData(new_body_buffer, true), Http::FilterDataStatus::Continue);

  // The response headers should have the new status.
  EXPECT_THAT(response_headers_, HeaderHasValueRef(Http::Headers::get().Status, "204"));

  // The filter should not encode any data.
  EXPECT_CALL(encoder_callbacks_, addEncodedData).Times(0);

  // If a cache getBody callback is made, it should be posted to the
  // dispatcher. Run events on the dispatcher so that any available callbacks
  // are invoked.
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
}

TEST_F(CacheFilterTest, SingleSatisfiableRange) {
  request_headers_.setHost("SingleSatisfiableRange");
  waitBeforeLookup();
  // Add range info to headers.
  request_headers_.addReference(Http::Headers::get().Range, "bytes=-2");

  response_headers_.setStatus(static_cast<uint64_t>(Http::Code::PartialContent));
  response_headers_.addReference(Http::Headers::get().ContentRange, "bytes 1-2/3");
  response_headers_.setContentLength(2);

  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::Ok,
                /*headers_=*/nullptr,
                /*content_length_=*/kBody.size(),
                /*range_details_=*/
                RangeDetails{/*satisfiable_=*/true,
                             /*ranges_=*/{AdjustedByteRange(1, 3)}}},
               [](MockLookupContext& lookup_context) {
                 EXPECT_CALL(lookup_context, getBody(AdjustedByteRange(1, 3), _))
                     .WillOnce(getBodyChunk(kBody.substr(1)));
               });
  EXPECT_CALL(decoder_callbacks_, continueDecoding);

  // Decode request 2 header
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(AllOf(IsSupersetOfHeaders(response_headers_),
                                   HeaderHasValueRef(Http::CustomHeaders::get().Age, age_)),
                             false));

  EXPECT_CALL(decoder_callbacks_, encodeData(bufferContains("bc"), true));
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // The cache lookup callback should be posted to the dispatcher. Run events on
  // the dispatcher so that the callback is invoked. The posted lookup callback
  // will cause another callback to be posted (when getBody() is called) which
  // should also be invoked.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(CacheFilterTest, MultipleSatisfiableRanges) {
  request_headers_.setHost("MultipleSatisfiableRanges");
  waitBeforeLookup();

  // Add range info to headers
  // multi-part responses are not supported, 200 expected
  request_headers_.addReference(Http::Headers::get().Range, "bytes=0-1,-2");

  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::Ok,
                /*headers_=*/nullptr,
                /*content_length_=*/kBody.size(),
                /*range_details_=*/
                RangeDetails{/*satisfiable_=*/true,
                             /*ranges_=*/{AdjustedByteRange(0, 1), AdjustedByteRange(1, 3)}}},
               [](MockLookupContext& lookup_context) {
                 // Fall back to looking up the entire body.
                 EXPECT_CALL(lookup_context, getBody(AdjustedByteRange(0, kBody.size()), _))
                     .WillOnce(getBodyChunk(kBody));
               });

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(isSupersetOfResponseHeadersWithAge(), false));

  EXPECT_CALL(decoder_callbacks_, encodeData(bufferContains(kBody), true));
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // The cache lookup callback should be posted to the dispatcher.
  // Run events on the dispatcher so that the callback is invoked.
  // The posted lookup callback will cause another callback to be posted (when getBody() is
  // called) which should also be invoked.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(CacheFilterTest, NotSatisfiableRange) {
  request_headers_.setHost("NotSatisfiableRange");
  waitBeforeLookup();
  // Add range info to headers
  request_headers_.addReference(Http::Headers::get().Range, "bytes=123-");

  response_headers_.setStatus(static_cast<uint64_t>(Http::Code::RangeNotSatisfiable));
  response_headers_.addReference(Http::Headers::get().ContentRange, "bytes */3");
  response_headers_.setContentLength(0);

  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::Ok,
                /*headers_=*/nullptr,
                /*content_length_=*/kBody.size(),
                /*range_details_=*/RangeDetails{/*satisfiable_=*/false}});
  EXPECT_CALL(decoder_callbacks_, continueDecoding);

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(isSupersetOfResponseHeadersWithAge(), true))
      .WillOnce(Invoke(filter_.get(), &CacheFilter::encodeHeaders));

  // 416 response should not have a body, so we don't expect a call to encodeData.
  EXPECT_CALL(decoder_callbacks_, encodeData).Times(0);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // The cache lookup callback should be posted to the dispatcher.
  // Run events on the dispatcher so that the callback is invoked.
  // The posted lookup callback will cause another callback to be posted (when getBody() is
  // called) which should also be invoked.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Send a GET request with a body. The CacheFilter will just pass everything through.
TEST_F(CacheFilterTest, GetRequestWithBodyAndTrailers) {
  request_headers_.setHost("GetRequestWithBodyAndTrailers");
  Buffer::OwnedImpl request_buffer(kBody);
  Http::TestRequestTrailerMapImpl request_trailers;

  EXPECT_EQ(decodeHeadersAndRunDispatcher(false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->decodeData(request_buffer, false), Http::FilterDataStatus::Continue);
  EXPECT_EQ(filter_->decodeTrailers(request_trailers), Http::FilterTrailersStatus::Continue);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
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
  // Create filter for request 2.
  CacheFilterSharedPtr filter = makeFilter();

  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::Ok,
                /*headers_=*/nullptr,
                /*content_length_=*/0});

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
}

// A new type alias for a different type of tests that use the exact same class
using ValidationHeadersTest = CacheFilterTest;

TEST_F(ValidationHeadersTest, EtagAndLastModified) {
  request_headers_.setHost("EtagAndLastModified");
  const std::string etag = "abc123";

  response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                    formatter_.now(time_source_));

  CacheFilterSharedPtr filter = makeFilter();

  // Make sure the request requires validation
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::RequiresValidation,
                /*headers_=*/nullptr,
                /*content_length_=*/0});
  EXPECT_CALL(decoder_callbacks_, continueDecoding);
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream*/ true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Make sure validation conditional headers are added
  const Http::TestRequestHeaderMapImpl injected_headers = {
      {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
  EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
}

TEST_F(ValidationHeadersTest, EtagOnly) {
  request_headers_.setHost("EtagOnly");
  const std::string etag = "abc123";

  // Add validation headers to the response
  response_headers_.setReferenceKey(Http::CustomHeaders::get().Etag, etag);

  // Make sure the request requires validation
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");

  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::RequiresValidation,
                /*headers_=*/nullptr,
                /*content_length_=*/0});
  EXPECT_CALL(decoder_callbacks_, continueDecoding);
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream*/ true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Make sure validation conditional headers are added
  // If-Modified-Since falls back to date
  const Http::TestRequestHeaderMapImpl injected_headers = {
      {"if-none-match", "abc123"}, {"if-modified-since", formatter_.now(time_source_)}};
  EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
}

TEST_F(ValidationHeadersTest, LastModifiedOnly) {
  request_headers_.setHost("LastModifiedOnly");

  // Add validation headers to the response
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified,
                                    formatter_.now(time_source_));

  // Make sure the request requires validation
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::RequiresValidation,
                /*headers_=*/nullptr,
                /*content_length_=*/0});
  EXPECT_CALL(decoder_callbacks_, continueDecoding);
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream*/ true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Make sure validation conditional headers are added
  const Http::TestRequestHeaderMapImpl injected_headers = {
      {"if-modified-since", formatter_.now(time_source_)}};
  EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
}

TEST_F(ValidationHeadersTest, NoEtagOrLastModified) {
  request_headers_.setHost("NoEtagOrLastModified");

  // Make sure the request requires validation
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::RequiresValidation,
                /*headers_=*/nullptr,
                /*content_length_=*/0});
  EXPECT_CALL(decoder_callbacks_, continueDecoding);
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream*/ true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Make sure validation conditional headers are added
  // If-Modified-Since falls back to date
  const Http::TestRequestHeaderMapImpl injected_headers = {
      {"if-modified-since", formatter_.now(time_source_)}};
  EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
}

TEST_F(ValidationHeadersTest, InvalidLastModified) {
  request_headers_.setHost("InvalidLastModified");

  // Add validation headers to the response
  response_headers_.setReferenceKey(Http::CustomHeaders::get().LastModified, "invalid-date");

  // Make sure the request requires validation
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
  expectLookup({/*cache_entry_status_=*/CacheEntryStatus::RequiresValidation,
                /*headers_=*/nullptr,
                /*content_length_=*/0});
  EXPECT_CALL(decoder_callbacks_, continueDecoding);
  EXPECT_EQ(decodeHeadersAndRunDispatcher(/*end_stream*/ true),
            Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark);

  // Make sure validation conditional headers are added
  // If-Modified-Since falls back to date
  const Http::TestRequestHeaderMapImpl injected_headers = {
      {"if-modified-since", formatter_.now(time_source_)}};
  EXPECT_THAT(request_headers_, IsSupersetOfHeaders(injected_headers));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

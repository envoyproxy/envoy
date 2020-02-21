#include "extensions/filters/http/cache/cache_filter.h"
#include "extensions/filters/http/cache/simple_http_cache/simple_http_cache.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

// Wrapper for SimpleHttpCache that delays the onHeaders callback from getHeaders, for verifying
// that CacheFilter works correctly whether the onHeaders call happens immediately, or after
// getHeaders and decodeHeaders return.
class DelayedCache : public SimpleHttpCache {
public:
  // HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request) override {
    return std::make_unique<DelayedLookupContext>(
        SimpleHttpCache::makeLookupContext(std::move(request)), delayed_cb_);
  }
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) override {
    return SimpleHttpCache::makeInsertContext(
        std::move(dynamic_cast<DelayedLookupContext&>(*lookup_context).context_));
  }

  std::function<void()> delayed_cb_;

private:
  class DelayedLookupContext : public LookupContext {
  public:
    DelayedLookupContext(LookupContextPtr&& context, std::function<void()>& delayed_cb)
        : context_(std::move(context)), delayed_cb_(delayed_cb) {}
    void getHeaders(LookupHeadersCallback&& cb) override {
      delayed_cb_ = [this, cb]() mutable { context_->getHeaders(std::move(cb)); };
    }
    void getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) override {
      context_->getBody(range, std::move(cb));
    }
    void getTrailers(LookupTrailersCallback&& cb) override { context_->getTrailers(std::move(cb)); }

    LookupContextPtr context_;
    std::function<void()>& delayed_cb_;
  };
};

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

TEST_F(CacheFilterTest, ImmediateHitNoBody) {
  request_headers_.setHost("ImmediateHitNoBody");
  ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(ReturnRef(context_.dispatcher_));
  ON_CALL(context_.dispatcher_, post(_)).WillByDefault(::testing::InvokeArgument<0>());

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 1 header
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);

    // Encode response header
    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 2 header
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                              HeaderHasValueRef("age", "0")),
                               true));
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, DelayedHitNoBody) {
  request_headers_.setHost("ImmediateHitNoBody");
  ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(ReturnRef(context_.dispatcher_));
  ON_CALL(context_.dispatcher_, post(_)).WillByDefault(::testing::InvokeArgument<0>());

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter(delayed_cache_);

    // Decode request 1 header
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    EXPECT_CALL(decoder_callbacks_, continueDecoding);
    delayed_cache_.delayed_cb_();
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

    // Encode response header
    EXPECT_EQ(filter.encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter(delayed_cache_);

    // Decode request 2 header
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                              HeaderHasValueRef("age", "0")),
                               true));
    delayed_cache_.delayed_cb_();
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
    filter.onDestroy();
  }
}

TEST_F(CacheFilterTest, ImmediateHitBody) {
  request_headers_.setHost("ImmediateHitBody");
  ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(ReturnRef(context_.dispatcher_));
  ON_CALL(context_.dispatcher_, post(_)).WillByDefault(::testing::InvokeArgument<0>());
  const std::string body = "abc";

  {
    // Create filter for request 1
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 1 header
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);

    // Encode response header
    Buffer::OwnedImpl buffer(body);
    response_headers_.setContentLength(body.size());
    EXPECT_EQ(filter.encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_EQ(filter.encodeData(buffer, true), Http::FilterDataStatus::Continue);
    filter.onDestroy();
  }
  {
    // Create filter for request 2
    CacheFilter filter = makeFilter(simple_cache_);

    // Decode request 2 header
    EXPECT_CALL(decoder_callbacks_,
                encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                              HeaderHasValueRef("age", "0")),
                               false));
    EXPECT_CALL(
        decoder_callbacks_,
        encodeData(testing::Property(&Buffer::Instance::toString, testing::Eq(body)), true));
    EXPECT_EQ(filter.decodeHeaders(request_headers_, true),
              Http::FilterHeadersStatus::StopAllIterationAndWatermark);
    ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
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

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

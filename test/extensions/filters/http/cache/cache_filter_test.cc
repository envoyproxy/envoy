#include "extensions/filters/http/cache/cache_filter.h"

#include "test/mocks/server/mocks.h"
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
  envoy::config::filter::http::cache::v2alpha::Cache config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Event::SimulatedTimeSystem time_source_;
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestHeaderMapImpl request_headers_{{":authority", "host"},
                                           {":path", "/"},
                                           {":method", "GET"},
                                           {"x-forwarded-proto", "https"}};
  Http::TestHeaderMapImpl response_headers_{{"date", formatter_.now(time_source_)},
                                            {"cache-control", "public,max-age=3600"}};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;

  CacheFilterSharedPtr makeFilter() {
    CacheFilterSharedPtr filter =
        CacheFilter::make(config_, /*stats_prefix=*/"", context_.scope(), context_.timeSource());
    if (filter) {
      filter->setDecoderFilterCallbacks(decoder_callbacks_);
      filter->setEncoderFilterCallbacks(encoder_callbacks_);
    }
    return filter;
  }
};

TEST_F(CacheFilterTest, ImmediateHit) {
  ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(ReturnRef(context_.dispatcher_));
  ON_CALL(context_.dispatcher_, post(_)).WillByDefault(::testing::InvokeArgument<0>());

  // Create filter for request 1
  config_.set_name("SimpleHttpCache");
  CacheFilterSharedPtr filter = makeFilter();
  ASSERT_TRUE(filter);

  // Decode request 1 header
  EXPECT_CALL(decoder_callbacks_, continueDecoding);
  EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
            Http::FilterHeadersStatus::StopIteration);
  ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);

  // Encode response header
  EXPECT_EQ(filter->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
  filter->onDestroy();

  // Create filter for request 2
  filter = makeFilter();
  ASSERT_TRUE(filter);

  // Decode request 2 header
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(testing::AllOf(IsSupersetOfHeaders(response_headers_),
                                                                HeaderHasValueRef("age", "0")), true));
  EXPECT_EQ(filter->decodeHeaders(request_headers_, true),
            Http::FilterHeadersStatus::StopIteration);
  ::testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
  filter->onDestroy();
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/kill_request/kill_request_filter.h"

#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {
namespace {

using ::testing::AnyNumber;
using ::testing::Return;

class KillRequestFilterTest : public testing::Test {
protected:
  void
  setUpTest(const envoy::extensions::filters::http::kill_request::v3::KillRequest& kill_request) {
    filter_ = std::make_unique<KillRequestFilter>(kill_request, random_generator_);

    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
    EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, pushTrackedObject(_)).Times(AnyNumber());
    EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, popTrackedObject(_)).Times(AnyNumber());
  }

  std::unique_ptr<KillRequestFilter> filter_;
  testing::NiceMock<Random::MockRandomGenerator> random_generator_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
};

TEST_F(KillRequestFilterTest, KillRequestCrashEnvoy) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "true");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_DEATH(filter_->decodeHeaders(request_headers_, false),
               "KillRequestFilter is crashing Envoy!!!");
}

TEST_F(KillRequestFilterTest, KillRequestCrashEnvoyWithCustomKillHeader) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  kill_request.set_kill_request_header("x-custom-kill-request");
  setUpTest(kill_request);
  request_headers_.addCopy("x-custom-kill-request", "true");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_DEATH(filter_->decodeHeaders(request_headers_, false),
               "KillRequestFilter is crashing Envoy!!!");
}

TEST_F(KillRequestFilterTest, KillRequestWithMillionDenominatorCrashEnvoy) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  kill_request.mutable_probability()->set_denominator(envoy::type::v3::FractionalPercent::MILLION);
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "yes");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_DEATH(filter_->decodeHeaders(request_headers_, false),
               "KillRequestFilter is crashing Envoy!!!");
}

TEST_F(KillRequestFilterTest, KillRequestDisabledWhenIsKillRequestEnabledReturnsFalse) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(0);
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "true");

  ON_CALL(random_generator_, random()).WillByDefault(Return(1));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Kill request should be enabled when isKillRequestEnabled returns true
// from the route level configuration.
TEST_F(KillRequestFilterTest, KillRequestEnabledFromRouteLevelConfiguration) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(0);
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "true");

  envoy::extensions::filters::http::kill_request::v3::KillRequest route_level_kill_request;
  route_level_kill_request.mutable_probability()->set_numerator(1);
  route_level_kill_request.set_kill_request_header("x-custom-kill-request");

  KillSettings kill_settings = KillSettings(route_level_kill_request);

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&kill_settings));
  EXPECT_DEATH(filter_->decodeHeaders(request_headers_, false), "");
}

// Kill request should be disabled when isKillRequestEnabled returns false
// from the route level configuration.
TEST_F(KillRequestFilterTest, KillRequestDisabledRouteLevelConfiguration) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(0);
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "true");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(nullptr));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

TEST_F(KillRequestFilterTest, KillRequestDisabledWhenHeaderIsMissing) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(100);
  setUpTest(kill_request);

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

TEST_F(KillRequestFilterTest, KillRequestDisabledWhenHeaderValueIsInvalid) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(100);
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "invalid");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

TEST_F(KillRequestFilterTest, DecodeDataReturnsContinue) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  setUpTest(kill_request);
  Buffer::OwnedImpl data;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
}

TEST_F(KillRequestFilterTest, DecodeTrailersReturnsContinue) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  setUpTest(kill_request);
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

TEST_F(KillRequestFilterTest, Encode1xxHeadersReturnsContinue) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  setUpTest(kill_request);
  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers));
}

TEST_F(KillRequestFilterTest, EncodeTrailersReturnsContinue) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  setUpTest(kill_request);
  Http::TestResponseTrailerMapImpl response_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
}

TEST_F(KillRequestFilterTest, EncodeMetadataReturnsContinue) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  setUpTest(kill_request);
  Http::MetadataMap metadata_map;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
}

TEST_F(KillRequestFilterTest, CanKillOnResponse) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  kill_request.set_direction(
      envoy::extensions::filters::http::kill_request::v3::KillRequest::RESPONSE);
  setUpTest(kill_request);

  // We should crash on the OUTBOUND request
  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  response_headers_.addCopy("x-envoy-kill-request", "true");
  EXPECT_DEATH(filter_->encodeHeaders(response_headers_, false),
               "KillRequestFilter is crashing Envoy!!!");
}

TEST_F(KillRequestFilterTest, KillsBasedOnDirection) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  kill_request.set_direction(
      envoy::extensions::filters::http::kill_request::v3::KillRequest::RESPONSE);
  setUpTest(kill_request);

  // We shouldn't crash on the REQUEST direction kill request
  request_headers_.addCopy("x-envoy-kill-request", "true");
  ASSERT_EQ(filter_->decodeHeaders(request_headers_, false), Http::FilterHeadersStatus::Continue);

  // We should crash on the RESPONSE kill request
  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  response_headers_.addCopy("x-envoy-kill-request", "true");
  EXPECT_DEATH(filter_->encodeHeaders(response_headers_, false),
               "KillRequestFilter is crashing Envoy!!!");
}

TEST_F(KillRequestFilterTest, PerRouteKillSettingFound) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "true");

  envoy::extensions::filters::http::kill_request::v3::KillRequest route_level_kill_request;
  // Set probability to zero to make isKillRequestEnabled return false
  route_level_kill_request.mutable_probability()->set_numerator(0);
  route_level_kill_request.set_kill_request_header("x-custom-kill-request");

  // Return valid kill setting on the REQUEST direction
  const KillSettings kill_settings(route_level_kill_request);
  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&kill_settings));
  ASSERT_EQ(filter_->decodeHeaders(request_headers_, false), Http::FilterHeadersStatus::Continue);
}

} // namespace
} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

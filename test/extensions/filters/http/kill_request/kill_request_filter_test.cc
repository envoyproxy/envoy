#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/kill_request/kill_request_filter.h"

#include "test/mocks/common.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {
namespace {

using ::testing::Return;

class KillRequestFilterTest : public testing::Test {
protected:
  void
  setUpTest(const envoy::extensions::filters::http::kill_request::v3::KillRequest& kill_request) {
    filter_ = std::make_unique<KillRequestFilter>(kill_request, random_generator_);
  }

  std::unique_ptr<KillRequestFilter> filter_;
  testing::NiceMock<Random::MockRandomGenerator> random_generator_;
  Http::TestRequestHeaderMapImpl request_headers_;
};

TEST_F(KillRequestFilterTest, KillRequestCrashEnvoy) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "true");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_DEATH(filter_->decodeHeaders(request_headers_, false), "");
}

TEST_F(KillRequestFilterTest, KillRequestCrashEnvoyWithCustomKillHeader) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  kill_request.set_kill_request_header("x-custom-kill-request");
  setUpTest(kill_request);
  request_headers_.addCopy("x-custom-kill-request", "true");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_DEATH(filter_->decodeHeaders(request_headers_, false), "");
}

TEST_F(KillRequestFilterTest, KillRequestWithMillionDenominatorCrashEnvoy) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  kill_request.mutable_probability()->set_denominator(envoy::type::v3::FractionalPercent::MILLION);
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "yes");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_DEATH(filter_->decodeHeaders(request_headers_, false), "");
}

TEST_F(KillRequestFilterTest, KillRequestDisabledWhenIsKillRequestEnabledReturnsFalse) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(0);
  setUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "true");

  ON_CALL(random_generator_, random()).WillByDefault(Return(1));
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

TEST_F(KillRequestFilterTest, Encode100ContinueHeadersReturnsContinue) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  setUpTest(kill_request);
  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(response_headers));
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

} // namespace
} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

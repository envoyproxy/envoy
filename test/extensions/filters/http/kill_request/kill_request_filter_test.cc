#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "extensions/filters/common/fault/fault_config.h"
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
  SetUpTest(const envoy::extensions::filters::http::kill_request::v3::KillRequest& kill_request) {
    filter_ = std::make_unique<KillRequestFilter>(kill_request, random_generator_);
  }

  std::unique_ptr<KillRequestFilter> filter_;
  Random::MockRandomGenerator random_generator_;
  Http::TestRequestHeaderMapImpl request_headers_;
};

TEST_F(KillRequestFilterTest, KillRequestCrashEnvoy) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  SetUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "true");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_DEATH(filter_->decodeHeaders(request_headers_, false), "");
}

TEST_F(KillRequestFilterTest, KillRequestWithMillionDenominatorCrashEnvoy) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(1);
  kill_request.mutable_probability()->set_denominator(envoy::type::v3::FractionalPercent::MILLION);
  SetUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "yes");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_DEATH(filter_->decodeHeaders(request_headers_, false), "");
}

TEST_F(KillRequestFilterTest, KillRequestDisabledWhenIsKillRequestEnabledReturnsFalse) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(0);
  SetUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "true");

  ON_CALL(random_generator_, random()).WillByDefault(Return(1));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

TEST_F(KillRequestFilterTest, KillRequestDisabledWhenHeaderIsMissing) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(100);
  SetUpTest(kill_request);

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

TEST_F(KillRequestFilterTest, KillRequestDisabledWhenHeaderValueIsInvalid) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(100);
  SetUpTest(kill_request);
  request_headers_.addCopy("x-envoy-kill-request", "invalid");

  ON_CALL(random_generator_, random()).WillByDefault(Return(0));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

} // namespace
} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

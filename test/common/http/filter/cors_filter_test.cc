#include <chrono>
#include <memory>

#include "envoy/event/dispatcher.h"

#include "common/http/filter/cors_filter.h"
#include "common/http/header_map_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Http {

class CorsFilterTest : public testing::Test {
public:
  CorsFilterTest() : config_{new CorsFilterConfig{}}, filter_(config_) {
    cors_policy_.reset(new Router::TestCorsPolicy());
    cors_policy_->enabled_ = true;
    cors_policy_->allow_origin_ = "*";
    cors_policy_->allow_methods_ = "GET";
    cors_policy_->allow_headers_ = "content-type";
    cors_policy_->expose_headers_ = "content-type";
    cors_policy_->allow_credentials_ = false;
    cors_policy_->max_age_ = "0";

    ON_CALL(decoder_callbacks_.route_->route_entry_, corsPolicy())
        .WillByDefault(ReturnRef(*cors_policy_));

    ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, corsPolicy())
        .WillByDefault(ReturnRef(*cors_policy_));

    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  NiceMock<MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::shared_ptr<CorsFilterConfig> config_;
  CorsFilter filter_;
  Buffer::OwnedImpl data_;
  TestHeaderMapImpl request_headers_;
  std::shared_ptr<Router::TestCorsPolicy> cors_policy_;
};

TEST_F(CorsFilterTest, NonOptionsRequest) {
  Http::TestHeaderMapImpl request_headers{{"method", "get"}};

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithoutOrigin) {
  Http::TestHeaderMapImpl request_headers{{"method", "options"}};

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  // EXPECT_EQ(false, filter_.is_cors_request_);
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOrigin) {
  Http::TestHeaderMapImpl request_headers{{"method", "options"}, {"origin", "localhost"}};

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  // EXPECT_EQ(true, filter_.is_cors_request_);
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsDisabled) {
  Http::TestHeaderMapImpl request_headers{{"method", "options"}, {"origin", "localhost"}};

  cors_policy_->enabled_ = false;

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsEnabled) {
  Http::TestHeaderMapImpl request_headers{{"method", "options"}, {"origin", "localhost"}};

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, ValidOptionsRequestWithAllowCredentialsTrue) {
  Http::TestHeaderMapImpl request_headers{{"method", "options"}, {"origin", "localhost"}};

  cors_policy_->allow_credentials_ = true;

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, ValidOptionsRequestWithAllowCredentialsFalse) {
  Http::TestHeaderMapImpl request_headers{{"method", "options"}, {"origin", "localhost"}};

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, ValidOptionsRequestWithoutRequestMethod) {
  Http::TestHeaderMapImpl request_headers{{"method", "options"}, {"origin", "localhost"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));

  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, ValidOptionsRequest) {
  Http::TestHeaderMapImpl request_headers{
      {"method", "options"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::HeaderMap& headers, bool end_stream) {
        EXPECT_STREQ("*", headers.AccessControlAllowOrigin()->value().c_str());
        EXPECT_STREQ("GET", headers.AccessControlAllowMethods()->value().c_str());
        EXPECT_STREQ("content-type", headers.AccessControlAllowHeaders()->value().c_str());
        EXPECT_STREQ("content-type", headers.AccessControlExposeHeaders()->value().c_str());
        EXPECT_STREQ("0", headers.AccessControlMaxAge()->value().c_str());
        EXPECT_TRUE(end_stream);
      }));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_.decodeHeaders(request_headers, false));

  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeWithCorsDisabled) {
  cors_policy_->enabled_ = false;

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeNonCorsRequest) {
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeWithAllowCredentialsTrue) {
  Http::TestHeaderMapImpl request_headers{{"origin", "localhost"}};
  cors_policy_->allow_credentials_ = true;

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("localhost", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("true", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeWithAllowCredentialsFalse) {
  Http::TestHeaderMapImpl request_headers{{"origin", "localhost"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("*", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

} // namespace Http
} // namespace Envoy

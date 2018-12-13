#include "common/http/header_map_impl.h"

#include "extensions/filters/http/cors/cors_filter.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

class CorsFilterTest : public testing::Test {
public:
  CorsFilterTest() : config_(new CorsFilterConfig("test.", stats_)), filter_(config_) {
    cors_policy_ = std::make_unique<Router::TestCorsPolicy>();
    cors_policy_->enabled_ = true;
    cors_policy_->allow_origin_.emplace_back("*");
    cors_policy_->allow_methods_ = "GET";
    cors_policy_->allow_headers_ = "content-type";
    cors_policy_->expose_headers_ = "content-type";
    cors_policy_->allow_credentials_ = false;
    cors_policy_->max_age_ = "0";

    ON_CALL(decoder_callbacks_.route_->route_entry_, corsPolicy())
        .WillByDefault(Return(cors_policy_.get()));

    ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, corsPolicy())
        .WillByDefault(Return(cors_policy_.get()));

    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  bool IsCorsRequest() { return filter_.is_cors_request_; }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Stats::IsolatedStoreImpl stats_;
  CorsFilterConfigSharedPtr config_;
  CorsFilter filter_;
  Buffer::OwnedImpl data_;
  Http::TestHeaderMapImpl request_headers_;
  std::unique_ptr<Router::TestCorsPolicy> cors_policy_;
  Router::MockDirectResponseEntry direct_response_entry_;
};

TEST_F(CorsFilterTest, RequestWithoutOrigin) {
  Http::TestHeaderMapImpl request_headers{{":method", "get"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, RequestWithOrigin) {
  Http::TestHeaderMapImpl request_headers{{":method", "get"}, {"origin", "localhost"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithoutOrigin) {
  Http::TestHeaderMapImpl request_headers{{":method", "OPTIONS"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOrigin) {
  Http::TestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsDisabled) {
  Http::TestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  cors_policy_->enabled_ = false;

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsEnabled) {
  Http::TestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithoutAccessRequestMethod) {
  Http::TestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestMatchingOriginByWildcard) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "test-host"}, {"access-control-request-method", "GET"}};

  Http::TestHeaderMapImpl response_headers{
      {":status", "200"},
      {"access-control-allow-origin", "test-host"},
      {"access-control-allow-methods", "GET"},
      {"access-control-allow-headers", "content-type"},
      {"access-control-max-age", "0"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestNotMatchingOrigin) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "test-host"}, {"access-control-request-method", "GET"}};

  cors_policy_->allow_origin_.clear();
  cors_policy_->allow_origin_.emplace_back("localhost");

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestEmptyOriginList) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "test-host"}, {"access-control-request-method", "GET"}};

  cors_policy_->allow_origin_.clear();

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, ValidOptionsRequestWithAllowCredentialsTrue) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  cors_policy_->allow_credentials_ = true;
  cors_policy_->allow_origin_.clear();
  cors_policy_->allow_origin_.emplace_back("localhost");

  Http::TestHeaderMapImpl response_headers{
      {":status", "200"},
      {"access-control-allow-origin", "localhost"},
      {"access-control-allow-credentials", "true"},
      {"access-control-allow-methods", "GET"},
      {"access-control-allow-headers", "content-type"},
      {"access-control-max-age", "0"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, ValidOptionsRequestWithAllowCredentialsFalse) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  Http::TestHeaderMapImpl response_headers{
      {":status", "200"},
      {"access-control-allow-origin", "localhost"},
      {"access-control-allow-methods", "GET"},
      {"access-control-allow-headers", "content-type"},
      {"access-control-max-age", "0"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeWithCorsDisabled) {
  cors_policy_->enabled_ = false;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeNonCorsRequest) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeWithAllowCredentialsTrue) {
  Http::TestHeaderMapImpl request_headers{{"origin", "localhost"}};
  cors_policy_->allow_credentials_ = true;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("localhost", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("true", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeWithExposeHeaders) {
  Http::TestHeaderMapImpl request_headers{{"origin", "localhost"}};
  cors_policy_->expose_headers_ = "custom-header-1";

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("localhost", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("custom-header-1", response_headers.get_("access-control-expose-headers"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeWithAllowCredentialsFalse) {
  Http::TestHeaderMapImpl request_headers{{"origin", "localhost"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("localhost", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EncodeWithNonMatchingOrigin) {
  Http::TestHeaderMapImpl request_headers{{"origin", "test-host"}};

  cors_policy_->allow_origin_.clear();
  cors_policy_->allow_origin_.emplace_back("localhost");

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, RedirectRoute) {
  ON_CALL(*decoder_callbacks_.route_, directResponseEntry())
      .WillByDefault(Return(&direct_response_entry_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EmptyRoute) {
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, EmptyRouteEntry) {
  ON_CALL(*decoder_callbacks_.route_, routeEntry()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, NoCorsEntry) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  ON_CALL(decoder_callbacks_.route_->route_entry_, corsPolicy()).WillByDefault(Return(nullptr));

  ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, corsPolicy())
      .WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers));

  Http::TestHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, NoRouteCorsEntry) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  ON_CALL(decoder_callbacks_.route_->route_entry_, corsPolicy()).WillByDefault(Return(nullptr));

  Http::TestHeaderMapImpl response_headers{
      {":status", "200"},
      {"access-control-allow-origin", "localhost"},
      {"access-control-allow-methods", "GET"},
      {"access-control-allow-headers", "content-type"},
      {"access-control-max-age", "0"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, NoVHostCorsEntry) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  cors_policy_->allow_methods_ = "";

  ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, corsPolicy())
      .WillByDefault(Return(nullptr));

  Http::TestHeaderMapImpl response_headers{
      {":status", "200"},
      {"access-control-allow-origin", "localhost"},
      {"access-control-allow-headers", "content-type"},
      {"access-control-max-age", "0"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestMatchingOriginByRegex) {
  Http::TestHeaderMapImpl request_headers{{":method", "OPTIONS"},
                                          {"origin", "www.envoyproxy.io"},
                                          {"access-control-request-method", "GET"}};

  Http::TestHeaderMapImpl response_headers{
      {":status", "200"},
      {"access-control-allow-origin", "www.envoyproxy.io"},
      {"access-control-allow-methods", "GET"},
      {"access-control-allow-headers", "content-type"},
      {"access-control-max-age", "0"},
  };

  cors_policy_->allow_origin_.clear();
  cors_policy_->allow_origin_regex_.emplace_back(std::regex(".*"));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

TEST_F(CorsFilterTest, OptionsRequestNotMatchingOriginByRegex) {
  Http::TestHeaderMapImpl request_headers{{":method", "OPTIONS"},
                                          {"origin", "www.envoyproxy.com"},
                                          {"access-control-request-method", "GET"}};

  cors_policy_->allow_origin_.clear();
  cors_policy_->allow_origin_regex_.emplace_back(std::regex(".*.envoyproxy.io"));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(request_headers_));
}

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

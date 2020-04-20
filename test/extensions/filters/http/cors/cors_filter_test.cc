#include "envoy/type/matcher/v3/string.pb.h"

#include "common/common/matchers.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/http/cors/cors_filter.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {
namespace {

Matchers::StringMatcherPtr makeExactStringMatcher(const std::string& exact_match) {
  envoy::type::matcher::v3::StringMatcher config;
  config.set_exact(exact_match);
  return std::make_unique<Matchers::StringMatcherImpl>(config);
}

Matchers::StringMatcherPtr makeStdRegexStringMatcher(const std::string& regex) {
  envoy::type::matcher::v3::StringMatcher config;
  config.set_hidden_envoy_deprecated_regex(regex);
  return std::make_unique<Matchers::StringMatcherImpl>(config);
}

} // namespace

class CorsFilterTest : public testing::Test {
public:
  CorsFilterTest() : config_(new CorsFilterConfig("test.", stats_)), filter_(config_) {
    cors_policy_ = std::make_unique<Router::TestCorsPolicy>();
    cors_policy_->enabled_ = true;
    cors_policy_->shadow_enabled_ = false;
    cors_policy_->allow_origins_.emplace_back(makeExactStringMatcher("*"));
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
  Stats::TestUtil::TestStore stats_;
  CorsFilterConfigSharedPtr config_;
  CorsFilter filter_;
  Buffer::OwnedImpl data_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  std::unique_ptr<Router::TestCorsPolicy> cors_policy_;
  Router::MockDirectResponseEntry direct_response_entry_;
};

TEST_F(CorsFilterTest, RequestWithoutOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "get"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, RequestWithOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "get"}, {"origin", "localhost"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithoutOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "OPTIONS"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsDisabled) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  cors_policy_->enabled_ = false;

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsDisabledShadowDisabled) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  cors_policy_->enabled_ = false;

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsDisabledShadowEnabled) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  cors_policy_->enabled_ = false;
  cors_policy_->shadow_enabled_ = true;

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsEnabled) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithoutAccessRequestMethod) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestMatchingOriginByWildcard) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "test-host"}, {"access-control-request-method", "GET"}};

  Http::TestResponseHeaderMapImpl response_headers{
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
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsEnabledShadowDisabled) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "test-host"}, {"access-control-request-method", "GET"}};

  cors_policy_->enabled_ = true;

  Http::TestResponseHeaderMapImpl response_headers{
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
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestWithOriginCorsEnabledShadowEnabled) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "test-host"}, {"access-control-request-method", "GET"}};

  cors_policy_->shadow_enabled_ = true;

  Http::TestResponseHeaderMapImpl response_headers{
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
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestNotMatchingOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "test-host"}, {"access-control-request-method", "GET"}};

  cors_policy_->allow_origins_.clear();
  cors_policy_->allow_origins_.emplace_back(makeExactStringMatcher("localhost"));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestEmptyOriginList) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "test-host"}, {"access-control-request-method", "GET"}};

  cors_policy_->allow_origins_.clear();

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, ValidOptionsRequestWithAllowCredentialsTrue) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  cors_policy_->allow_credentials_ = true;
  cors_policy_->allow_origins_.clear();
  cors_policy_->allow_origins_.emplace_back(makeExactStringMatcher("localhost"));

  Http::TestResponseHeaderMapImpl response_headers{
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
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, ValidOptionsRequestWithAllowCredentialsFalse) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  Http::TestResponseHeaderMapImpl response_headers{
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
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, EncodeWithCorsDisabled) {
  cors_policy_->enabled_ = false;
  cors_policy_->shadow_enabled_ = false;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, EncodeNonCorsRequest) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, EncodeWithAllowCredentialsTrue) {
  Http::TestRequestHeaderMapImpl request_headers{{"origin", "localhost"}};
  cors_policy_->allow_credentials_ = true;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("localhost", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("true", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, EncodeWithExposeHeaders) {
  Http::TestRequestHeaderMapImpl request_headers{{"origin", "localhost"}};
  cors_policy_->expose_headers_ = "custom-header-1";

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.encodeMetadata(metadata_map));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("localhost", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("custom-header-1", response_headers.get_("access-control-expose-headers"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, EncodeWithAllowCredentialsFalse) {
  Http::TestRequestHeaderMapImpl request_headers{{"origin", "localhost"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("localhost", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, EncodeWithNonMatchingOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{"origin", "test-host"}};

  cors_policy_->allow_origins_.clear();
  cors_policy_->allow_origins_.emplace_back(makeExactStringMatcher("localhost"));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, RedirectRoute) {
  ON_CALL(*decoder_callbacks_.route_, directResponseEntry())
      .WillByDefault(Return(&direct_response_entry_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, EmptyRoute) {
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, EmptyRouteEntry) {
  ON_CALL(*decoder_callbacks_.route_, routeEntry()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, NoCorsEntry) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  ON_CALL(decoder_callbacks_.route_->route_entry_, corsPolicy()).WillByDefault(Return(nullptr));

  ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, corsPolicy())
      .WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("access-control-allow-origin"));
  EXPECT_EQ("", response_headers.get_("access-control-allow-credentials"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, NoRouteCorsEntry) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  ON_CALL(decoder_callbacks_.route_->route_entry_, corsPolicy()).WillByDefault(Return(nullptr));

  Http::TestResponseHeaderMapImpl response_headers{
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
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, NoVHostCorsEntry) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"}, {"origin", "localhost"}, {"access-control-request-method", "GET"}};

  cors_policy_->allow_methods_ = "";

  ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, corsPolicy())
      .WillByDefault(Return(nullptr));

  Http::TestResponseHeaderMapImpl response_headers{
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
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestMatchingOriginByRegex) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "OPTIONS"},
                                                 {"origin", "www.envoyproxy.io"},
                                                 {"access-control-request-method", "GET"}};

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"access-control-allow-origin", "www.envoyproxy.io"},
      {"access-control-allow-methods", "GET"},
      {"access-control-allow-headers", "content-type"},
      {"access-control-max-age", "0"},
  };

  cors_policy_->allow_origins_.clear();
  cors_policy_->allow_origins_.emplace_back(makeStdRegexStringMatcher(".*"));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(true, IsCorsRequest());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(CorsFilterTest, OptionsRequestNotMatchingOriginByRegex) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "OPTIONS"},
                                                 {"origin", "www.envoyproxy.com"},
                                                 {"access-control-request-method", "GET"}};

  cors_policy_->allow_origins_.clear();
  cors_policy_->allow_origins_.emplace_back(makeStdRegexStringMatcher(".*.envoyproxy.io"));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(false, IsCorsRequest());
  EXPECT_EQ(1, stats_.counter("test.cors.origin_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.cors.origin_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

// Test that the deprecated extension name still functions.
TEST(CorsFilterConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.cors";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

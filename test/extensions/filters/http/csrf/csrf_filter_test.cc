#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/csrf/csrf_filter.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
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
namespace Csrf {

class CsrfFilterTest : public testing::Test {
public:
  CsrfFilterConfigSharedPtr setupConfig() {
    envoy::extensions::filters::http::csrf::v3::CsrfPolicy policy;
    const auto& filter_enabled = policy.mutable_filter_enabled();
    filter_enabled->mutable_default_value()->set_numerator(100);
    filter_enabled->mutable_default_value()->set_denominator(
        envoy::type::v3::FractionalPercent::HUNDRED);
    filter_enabled->set_runtime_key("csrf.enabled");

    const auto& shadow_enabled = policy.mutable_shadow_enabled();
    shadow_enabled->mutable_default_value()->set_numerator(0);
    shadow_enabled->mutable_default_value()->set_denominator(
        envoy::type::v3::FractionalPercent::HUNDRED);
    shadow_enabled->set_runtime_key("csrf.shadow_enabled");

    const auto& add_exact_origin = policy.mutable_additional_origins()->Add();
    add_exact_origin->set_exact("additionalhost");

    const auto& add_regex_origin = policy.mutable_additional_origins()->Add();
    add_regex_origin->MergeFrom(TestUtility::createRegexMatcher(R"(www\-[0-9]\.allow\.com)"));

    return std::make_shared<CsrfFilterConfig>(policy, "test", *stats_.rootScope(),
                                              factory_context_);
  }

  CsrfFilterTest() : config_(setupConfig()), filter_(config_) {}

  void SetUp() override {
    setRoutePolicy(config_->policy());
    setVirtualHostPolicy(config_->policy());

    setFilterEnabled(true);
    setShadowEnabled(false);

    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void setRoutePolicy(const CsrfPolicy* policy) {
    ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(filter_name_))
        .WillByDefault(Return(policy));
  }

  void setVirtualHostPolicy(const CsrfPolicy* policy) {
    ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(filter_name_))
        .WillByDefault(Return(policy));
  }

  void setFilterEnabled(bool enabled) {
    ON_CALL(factory_context_.runtime_loader_.snapshot_,
            featureEnabled("csrf.enabled",
                           testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
        .WillByDefault(Return(enabled));
  }

  void setShadowEnabled(bool enabled) {
    ON_CALL(factory_context_.runtime_loader_.snapshot_,
            featureEnabled("csrf.shadow_enabled",
                           testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
        .WillByDefault(Return(enabled));
  }

  const std::string filter_name_ = "envoy.filters.http.csrf";
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  Buffer::OwnedImpl data_;
  Router::MockDirectResponseEntry direct_response_entry_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  CsrfFilterConfigSharedPtr config_;

  CsrfFilter filter_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
};

TEST_F(CsrfFilterTest, RequestWithNonMutableMethod) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithoutOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(1U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithoutDestination) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://localhost"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithInvalidOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "http://cross-origin"}, {":authority", "localhost"}};

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "403"},
      {"content-length", "14"},
      {"content-type", "text/plain"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
  EXPECT_EQ("csrf_origin_mismatch", decoder_callbacks_.details());
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginDifferentNonStandardPorts) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://localhost:90"},
                                                 {":authority", "localhost:91"},
                                                 {":scheme", "http"}};

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "403"},
      {"content-length", "14"},
      {"content-type", "text/plain"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
  EXPECT_EQ("csrf_origin_mismatch", decoder_callbacks_.details());
}

TEST_F(CsrfFilterTest, RequestWithValidOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://localhost"},
                                                 {"host", "localhost"},
                                                 {":scheme", "http"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithValidOriginNonStandardPort) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://localhost:88"},
                                                 {"host", "localhost:88"},
                                                 {":scheme", "http"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

// This works because gURL drops the port for hostAndPort() when they are standard
// ports (e.g.: 80 & 443).
TEST_F(CsrfFilterTest, RequestWithValidOriginHttpVsHttps) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "https://localhost"},
                                                 {"host", "localhost"},
                                                 {":scheme", "http"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginCsrfDisabledShadowDisabled) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "http://cross-origin"}, {"host", "localhost"}};

  setFilterEnabled(false);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginCsrfDisabledShadowEnabled) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://cross-origin"},
                                                 {"host", "localhost"},
                                                 {":scheme", "http"}};

  setFilterEnabled(false);
  setShadowEnabled(true);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithValidOriginCsrfDisabledShadowEnabled) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://localhost"},
                                                 {"host", "localhost"},
                                                 {":scheme", "http"}};

  setFilterEnabled(false);
  setShadowEnabled(true);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginCsrfEnabledShadowEnabled) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://cross-origin"},
                                                 {"host", "localhost"},
                                                 {":scheme", "http"}};

  setShadowEnabled(true);

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "403"},
      {"content-length", "14"},
      {"content-type", "text/plain"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithValidOriginCsrfEnabledShadowEnabled) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://localhost"},
                                                 {"host", "localhost"},
                                                 {":scheme", "http"}};

  setShadowEnabled(true);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RedirectRoute) {
  ON_CALL(*decoder_callbacks_.route_, directResponseEntry())
      .WillByDefault(Return(&direct_response_entry_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, EmptyRoute) {
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, EmptyRouteEntry) {
  ON_CALL(*decoder_callbacks_.route_, routeEntry()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, NoCsrfEntry) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://cross-origin"},
                                                 {"host", "localhost"},
                                                 {":scheme", "http"}};

  setRoutePolicy(nullptr);
  setVirtualHostPolicy(nullptr);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));
  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, NoRouteCsrfEntry) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {"origin", "http://localhost"}};

  setRoutePolicy(nullptr);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, NoVHostCsrfEntry) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "DELETE"},
                                                 {"origin", "http://localhost"}};

  setVirtualHostPolicy(nullptr);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestFromAdditionalExactOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://additionalhost"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestFromAdditionalRegexOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://www-1.allow.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestFromInvalidAdditionalRegexOrigin) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "PUT"},
                                                 {"origin", "http://www.allow.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

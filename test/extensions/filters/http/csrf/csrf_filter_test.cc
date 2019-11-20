#include "common/http/header_map_impl.h"

#include "extensions/filters/http/csrf/csrf_filter.h"

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
namespace Csrf {

class CsrfFilterTest : public testing::Test {
public:
  CsrfFilterConfigSharedPtr setupConfig() {
    envoy::config::filter::http::csrf::v2::CsrfPolicy policy;
    const auto& filter_enabled = policy.mutable_filter_enabled();
    filter_enabled->mutable_default_value()->set_numerator(100);
    filter_enabled->mutable_default_value()->set_denominator(
        envoy::type::FractionalPercent::HUNDRED);
    filter_enabled->set_runtime_key("csrf.enabled");

    const auto& shadow_enabled = policy.mutable_shadow_enabled();
    shadow_enabled->mutable_default_value()->set_numerator(0);
    shadow_enabled->mutable_default_value()->set_denominator(
        envoy::type::FractionalPercent::HUNDRED);
    shadow_enabled->set_runtime_key("csrf.shadow_enabled");

    const auto& add_exact_origin = policy.mutable_additional_origins()->Add();
    add_exact_origin->set_exact("additionalhost");

    const auto& add_regex_origin = policy.mutable_additional_origins()->Add();
    add_regex_origin->set_regex(R"(www\-[0-9]\.allow\.com)");

    return std::make_shared<CsrfFilterConfig>(policy, "test", stats_, runtime_);
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
    ON_CALL(decoder_callbacks_.route_->route_entry_, perFilterConfig(filter_name_))
        .WillByDefault(Return(policy));
  }

  void setVirtualHostPolicy(const CsrfPolicy* policy) {
    ON_CALL(decoder_callbacks_.route_->route_entry_, perFilterConfig(filter_name_))
        .WillByDefault(Return(policy));
  }

  void setFilterEnabled(bool enabled) {
    ON_CALL(
        runtime_.snapshot_,
        featureEnabled("csrf.enabled", testing::Matcher<const envoy::type::FractionalPercent&>(_)))
        .WillByDefault(Return(enabled));
  }

  void setShadowEnabled(bool enabled) {
    ON_CALL(runtime_.snapshot_,
            featureEnabled("csrf.shadow_enabled",
                           testing::Matcher<const envoy::type::FractionalPercent&>(_)))
        .WillByDefault(Return(enabled));
  }

  const std::string filter_name_ = "envoy.csrf";
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  Buffer::OwnedImpl data_;
  Router::MockDirectResponseEntry direct_response_entry_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  CsrfFilterConfigSharedPtr config_;

  CsrfFilter filter_;
  Http::TestHeaderMapImpl request_headers_;
};

TEST_F(CsrfFilterTest, RequestWithNonMutableMethod) {
  Http::TestHeaderMapImpl request_headers{{":method", "GET"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithoutOrigin) {
  Http::TestHeaderMapImpl request_headers{{":method", "PUT"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(1U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithoutDestination) {
  Http::TestHeaderMapImpl request_headers{{":method", "PUT"}, {"origin", "localhost"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithInvalidOrigin) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "cross-origin"}, {":authority", "localhost"}};

  Http::TestHeaderMapImpl response_headers{
      {":status", "403"},
      {"content-length", "14"},
      {"content-type", "text/plain"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
  EXPECT_EQ("csrf_origin_mismatch", decoder_callbacks_.details_);
}

TEST_F(CsrfFilterTest, RequestWithValidOrigin) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "localhost"}, {"host", "localhost"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginCsrfDisabledShadowDisabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "cross-origin"}, {"host", "localhost"}};

  setFilterEnabled(false);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginCsrfDisabledShadowEnabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "cross-origin"}, {"host", "localhost"}};

  setFilterEnabled(false);
  setShadowEnabled(true);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithValidOriginCsrfDisabledShadowEnabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "localhost"}, {"host", "localhost"}};

  setFilterEnabled(false);
  setShadowEnabled(true);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginCsrfEnabledShadowEnabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "cross-origin"}, {"host", "localhost"}};

  setShadowEnabled(true);

  Http::TestHeaderMapImpl response_headers{
      {":status", "403"},
      {"content-length", "14"},
      {"content-type", "text/plain"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestWithValidOriginCsrfEnabledShadowEnabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "localhost"}, {"host", "localhost"}};

  setShadowEnabled(true);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RedirectRoute) {
  ON_CALL(*decoder_callbacks_.route_, directResponseEntry())
      .WillByDefault(Return(&direct_response_entry_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, EmptyRoute) {
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, EmptyRouteEntry) {
  ON_CALL(*decoder_callbacks_.route_, routeEntry()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, NoCsrfEntry) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "cross-origin"}, {"host", "localhost"}};

  setRoutePolicy(nullptr);
  setVirtualHostPolicy(nullptr);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers));
  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, NoRouteCsrfEntry) {
  Http::TestHeaderMapImpl request_headers{{":method", "POST"}, {"origin", "localhost"}};

  setRoutePolicy(nullptr);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, NoVHostCsrfEntry) {
  Http::TestHeaderMapImpl request_headers{{":method", "DELETE"}, {"origin", "localhost"}};

  setVirtualHostPolicy(nullptr);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestFromAdditionalExactOrigin) {
  Http::TestHeaderMapImpl request_headers{{":method", "PUT"}, {"origin", "additionalhost"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestFromAdditionalRegexOrigin) {
  Http::TestHeaderMapImpl request_headers{{":method", "PUT"}, {"origin", "www-1.allow.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(0U, config_->stats().request_invalid_.value());
  EXPECT_EQ(1U, config_->stats().request_valid_.value());
}

TEST_F(CsrfFilterTest, RequestFromInvalidAdditionalRegexOrigin) {
  Http::TestHeaderMapImpl request_headers{{":method", "PUT"}, {"origin", "www.allow.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0U, config_->stats().missing_source_origin_.value());
  EXPECT_EQ(1U, config_->stats().request_invalid_.value());
  EXPECT_EQ(0U, config_->stats().request_valid_.value());
}
} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

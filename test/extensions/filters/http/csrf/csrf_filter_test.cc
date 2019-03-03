#include "common/http/header_map_impl.h"

#include "extensions/filters/http/csrf/csrf_filter.h"

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
namespace Csrf {

class CsrfFilterTest : public testing::Test {
public:
  CsrfFilterTest() : config_(new CsrfFilterConfig("test.", stats_)), filter_(config_) {
    csrf_policy_ = std::make_unique<Router::TestCsrfPolicy>();
    csrf_policy_->enabled_ = true;
    csrf_policy_->shadow_enabled_ = false;

    ON_CALL(decoder_callbacks_.route_->route_entry_, csrfPolicy())
        .WillByDefault(Return(csrf_policy_.get()));

    ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, csrfPolicy())
        .WillByDefault(Return(csrf_policy_.get()));

    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Stats::IsolatedStoreImpl stats_;
  CsrfFilterConfigSharedPtr config_;
  CsrfFilter filter_;
  Buffer::OwnedImpl data_;
  Http::TestHeaderMapImpl request_headers_;
  std::unique_ptr<Router::TestCsrfPolicy> csrf_policy_;
  Router::MockDirectResponseEntry direct_response_entry_;
};

TEST_F(CsrfFilterTest, RequestWithNonMutableMethod) {
  Http::TestHeaderMapImpl request_headers{{":method", "GET"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
}

TEST_F(CsrfFilterTest, RequestWithoutOrigin) {
  Http::TestHeaderMapImpl request_headers{{":method", "PUT"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(1, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
}

TEST_F(CsrfFilterTest, RequestWithoutDestination) {
  Http::TestHeaderMapImpl request_headers{{":method", "PUT"}, {"origin", "localhost"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(1, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
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
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(1, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
}

TEST_F(CsrfFilterTest, RequestWithValidOrigin) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "localhost"}, {"host", "localhost"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginCsrfDisabledShadowDisabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "cross-origin"}, {"host", "localhost"}};

  csrf_policy_->enabled_ = false;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginCsrfDisabledShadowEnabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "cross-origin"}, {"host", "localhost"}};

  csrf_policy_->enabled_ = false;
  csrf_policy_->shadow_enabled_ = true;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(1, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
}

TEST_F(CsrfFilterTest, RequestWithValidOriginCsrfDisabledShadowEnabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "localhost"}, {"host", "localhost"}};

  csrf_policy_->enabled_ = false;
  csrf_policy_->shadow_enabled_ = true;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
}

TEST_F(CsrfFilterTest, RequestWithInvalidOriginCsrfEnabledShadowEnabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "cross-origin"}, {"host", "localhost"}};

  csrf_policy_->shadow_enabled_ = true;

  Http::TestHeaderMapImpl response_headers{
      {":status", "403"},
      {"content-length", "14"},
      {"content-type", "text/plain"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(1, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
}

TEST_F(CsrfFilterTest, RequestWithValidOriginCsrfEnabledShadowEnabled) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "localhost"}, {"host", "localhost"}};

  csrf_policy_->shadow_enabled_ = true;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(1, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));
}

TEST_F(CsrfFilterTest, RedirectRoute) {
  ON_CALL(*decoder_callbacks_.route_, directResponseEntry())
      .WillByDefault(Return(&direct_response_entry_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
}

TEST_F(CsrfFilterTest, EmptyRoute) {
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
}

TEST_F(CsrfFilterTest, EmptyRouteEntry) {
  ON_CALL(*decoder_callbacks_.route_, routeEntry()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
}

TEST_F(CsrfFilterTest, NoCsrfEntry) {
  Http::TestHeaderMapImpl request_headers{
      {":method", "PUT"}, {"origin", "cross-origin"}, {"host", "localhost"}};

  ON_CALL(decoder_callbacks_.route_->route_entry_, csrfPolicy()).WillByDefault(Return(nullptr));

  ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, csrfPolicy())
      .WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers));
}

TEST_F(CsrfFilterTest, NoRouteCsrfEntry) {
  Http::TestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  ON_CALL(decoder_callbacks_.route_->route_entry_, csrfPolicy()).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
}

TEST_F(CsrfFilterTest, NoVHostCsrfEntry) {
  Http::TestHeaderMapImpl request_headers{{":method", "OPTIONS"}, {"origin", "localhost"}};

  ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, csrfPolicy())
      .WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_headers_));

  EXPECT_EQ(0, stats_.counter("test.csrf.missing_source_origin").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_invalid").value());
  EXPECT_EQ(0, stats_.counter("test.csrf.request_valid").value());
}
} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

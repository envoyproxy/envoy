#include "extensions/filters/http/tap/config.h"
#include "extensions/filters/http/tap/tap_filter.h"

#include "test/extensions/filters/http/tap/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {
namespace {

class MockFilterConfig : public FilterConfig {
public:
  MOCK_METHOD(HttpTapConfigSharedPtr, currentConfig, ());
  FilterStats& stats() override { return stats_; }

  Stats::IsolatedStoreImpl stats_store_;
  FilterStats stats_{Filter::generateStats("foo", stats_store_)};
};

class MockHttpPerRequestTapper : public HttpPerRequestTapper {
public:
  MOCK_METHOD(void, onRequestHeaders, (const Http::RequestHeaderMap& headers));
  MOCK_METHOD(void, onRequestBody, (const Buffer::Instance& data));
  MOCK_METHOD(void, onRequestTrailers, (const Http::RequestTrailerMap& headers));
  MOCK_METHOD(void, onResponseHeaders, (const Http::ResponseHeaderMap& headers));
  MOCK_METHOD(void, onResponseBody, (const Buffer::Instance& data));
  MOCK_METHOD(void, onResponseTrailers, (const Http::ResponseTrailerMap& headers));
  MOCK_METHOD(bool, onDestroyLog, ());
};

class TapFilterTest : public testing::Test {
public:
  void setup(bool has_config) {
    if (has_config) {
      http_tap_config_ = std::make_shared<MockHttpTapConfig>();
    }

    EXPECT_CALL(*filter_config_, currentConfig()).WillRepeatedly(Return(http_tap_config_));
    filter_ = std::make_unique<Filter>(filter_config_);

    if (has_config) {
      EXPECT_CALL(callbacks_, streamId());
      http_per_request_tapper_ = new MockHttpPerRequestTapper();
      EXPECT_CALL(*http_tap_config_, createPerRequestTapper_(_))
          .WillOnce(Return(http_per_request_tapper_));
    }

    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  std::shared_ptr<MockFilterConfig> filter_config_{new MockFilterConfig()};
  std::shared_ptr<MockHttpTapConfig> http_tap_config_;
  MockHttpPerRequestTapper* http_per_request_tapper_;
  std::unique_ptr<Filter> filter_;
  StreamInfo::MockStreamInfo stream_info_;
  Http::MockStreamDecoderFilterCallbacks callbacks_;
};

// Verify filter functionality when there is no tap config.
TEST_F(TapFilterTest, NoConfig) {
  InSequence s;
  setup(false);

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Buffer::OwnedImpl request_body;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_body, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(response_headers));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl response_body;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, false));
  Http::TestResponseTrailerMapImpl response_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  Http::MetadataMap metadata;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata));

  filter_->log(&request_headers, &response_headers, &response_trailers, stream_info_);
}

// Verify filter functionality when there is a tap config.
TEST_F(TapFilterTest, Config) {
  InSequence s;
  setup(true);

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_CALL(*http_per_request_tapper_, onRequestHeaders(_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Buffer::OwnedImpl request_body("hello");
  EXPECT_CALL(*http_per_request_tapper_, onRequestBody(_));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_body, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_CALL(*http_per_request_tapper_, onRequestTrailers(_));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(response_headers));
  EXPECT_CALL(*http_per_request_tapper_, onResponseHeaders(_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl response_body("hello");
  EXPECT_CALL(*http_per_request_tapper_, onResponseBody(_));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, false));
  Http::TestResponseTrailerMapImpl response_trailers;
  EXPECT_CALL(*http_per_request_tapper_, onResponseTrailers(_));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));

  EXPECT_CALL(*http_per_request_tapper_, onDestroyLog()).WillOnce(Return(true));
  filter_->log(&request_headers, &response_headers, &response_trailers, stream_info_);
  EXPECT_EQ(1UL, filter_config_->stats_.rq_tapped_.value());

  // Workaround InSequence/shared_ptr mock leak.
  EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(http_tap_config_.get()));
}

TEST(TapFilterConfigTest, InvalidProto) {
  const std::string filter_config =
      R"EOF(
  common_config:
    static_config:
      match_config:
        any_match: true
      output_config:
        sinks:
          - format: JSON_BODY_AS_STRING
            streaming_admin: {}
)EOF";

  envoy::extensions::filters::http::tap::v3::Tap config;
  TestUtility::loadFromYaml(filter_config, config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  TapFilterFactory factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(config, "stats", context),
                            EnvoyException,
                            "Error: Specifying admin streaming output without configuring admin.");
}

} // namespace
} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

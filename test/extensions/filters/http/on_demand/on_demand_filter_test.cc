#include <memory>

#include "common/http/header_map_impl.h"

#include "extensions/filters/http/on_demand/on_demand_update.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

class OnDemandFilterTest : public testing::Test {
public:
  void SetUp() override {
    filter_ = std::make_unique<OnDemandRouteUpdate>();
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  std::unique_ptr<OnDemandRouteUpdate> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

// tests decodeHeaders() when no cached route is available and vhds is configured
TEST_F(OnDemandFilterTest, TestDecodeHeaders) {
  Http::TestRequestHeaderMapImpl headers;
  std::shared_ptr<Router::MockConfig> route_config_ptr{new NiceMock<Router::MockConfig>()};
  EXPECT_CALL(decoder_callbacks_, route()).WillOnce(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_, routeConfig()).Times(2).WillRepeatedly(Return(route_config_ptr));
  EXPECT_CALL(*route_config_ptr, usesVhds()).WillOnce(Return(true));
  EXPECT_CALL(decoder_callbacks_, requestRouteConfigUpdate(_));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
}

// tests decodeHeaders() when no cached route is available
TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteAvailable) {
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

// tests decodeHeaders() when no route configuration is available
TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteConfigIsNotAvailable) {
  Http::TestRequestHeaderMapImpl headers;
  std::shared_ptr<Router::MockConfig> route_config_ptr{new NiceMock<Router::MockConfig>()};
  EXPECT_CALL(decoder_callbacks_, route()).WillOnce(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_, routeConfig()).WillOnce(Return(absl::nullopt));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeTrailers) {
  Http::TestRequestTrailerMapImpl headers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// tests decodeData() when filter state is Http::FilterHeadersStatus::Continue
TEST_F(OnDemandFilterTest, TestDecodeDataReturnsContinue) {
  Buffer::OwnedImpl buffer;
  filter_->setFilterIterationState(Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
}

// tests decodeData() when filter state is Http::FilterHeadersStatus::StopIteration
TEST_F(OnDemandFilterTest, TestDecodeDataReturnsStopIteration) {
  Buffer::OwnedImpl buffer;
  filter_->setFilterIterationState(Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer, false));
}

// tests onRouteConfigUpdateCompletion() route hasn't been resolved
TEST_F(OnDemandFilterTest,
       TestOnRouteConfigUpdateCompletionContinuesDecodingWhenRouteDoesNotExist) {
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->onRouteConfigUpdateCompletion(false);
}

// tests onRouteConfigUpdateCompletion() when redirect contains a body
TEST_F(OnDemandFilterTest, TestOnRouteConfigUpdateCompletionContinuesDecodingWithRedirectWithBody) {
  Buffer::OwnedImpl buffer;
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillOnce(Return(&buffer));
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onRouteConfigUpdateCompletion() when ActiveStream recreation fails
TEST_F(OnDemandFilterTest, OnRouteConfigUpdateCompletionContinuesDecodingIfRedirectFails) {
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillOnce(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_, recreateStream()).WillOnce(Return(false));
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onRouteConfigUpdateCompletion() when route was resolved
TEST_F(OnDemandFilterTest, OnRouteConfigUpdateCompletionRestartsActiveStream) {
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillOnce(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_, recreateStream()).WillOnce(Return(true));
  filter_->onRouteConfigUpdateCompletion(true);
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

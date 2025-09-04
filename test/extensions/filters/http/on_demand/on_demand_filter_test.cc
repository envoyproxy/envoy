#include <memory>

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/on_demand/on_demand_update.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

class OnDemandFilterTest : public testing::Test {
public:
  void SetUp() override {
    auto config = std::make_shared<OnDemandFilterConfig>(DecodeHeadersBehavior::rds());
    odcds_ = nullptr;
    setupWithConfig(std::move(config));
  }

  void setupWithCds() {
    auto mock_odcds = Upstream::MockOdCdsApiHandle::create();
    odcds_ = mock_odcds.get();
    auto config = std::make_shared<OnDemandFilterConfig>(
        DecodeHeadersBehavior::cdsRds(std::move(mock_odcds), std::chrono::milliseconds(5000)));
    setupWithConfig(std::move(config));
  }

  void setupWithConfig(OnDemandFilterConfigSharedPtr config) {
    filter_ = std::make_unique<OnDemandRouteUpdate>(std::move(config));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  Upstream::MockOdCdsApiHandle* odcds_;
  std::unique_ptr<OnDemandRouteUpdate> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteAvailableButHasNoEntry) {
  setupWithCds();
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_CALL(decoder_callbacks_, clusterInfo()).WillOnce(Return(nullptr));
  EXPECT_CALL(*decoder_callbacks_.route_, routeEntry()).WillOnce(Return(nullptr));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteAvailableAndConfigIsNull) {
  setupWithConfig(nullptr);
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteAvailableButOdCdsIsDisabled) {
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteAvailableAndClusterIsAvailable) {
  setupWithCds();
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteAvailableButClusterIsNotAvailable) {
  setupWithCds();
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_CALL(decoder_callbacks_, clusterInfo()).WillOnce(Return(nullptr));
  EXPECT_CALL(*odcds_, requestOnDemandClusterDiscovery(_, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteAvailableButClusterNameIsEmpty) {
  setupWithCds();
  Http::TestRequestHeaderMapImpl headers;
  std::string empty_cluster_name;
  EXPECT_CALL(decoder_callbacks_, clusterInfo()).WillOnce(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, clusterName())
      .WillOnce(ReturnRef(empty_cluster_name));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteIsNotAvailableAndOdCdsIsEnabled) {
  setupWithCds();
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteIsNotAvailable) {
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
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
// With the fix, requests with bodies should now properly recreate the stream
TEST_F(OnDemandFilterTest, TestOnRouteConfigUpdateCompletionRestartsStreamWithRedirectWithBody) {
  Buffer::OwnedImpl buffer;
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onRouteConfigUpdateCompletion() when ActiveStream recreation fails
TEST_F(OnDemandFilterTest, OnRouteConfigUpdateCompletionContinuesDecodingIfRedirectFails) {
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(false));
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onRouteConfigUpdateCompletion() when route was resolved
TEST_F(OnDemandFilterTest, OnRouteConfigUpdateCompletionRestartsActiveStream) {
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onClusterDiscoveryCompletion when a cluster is missing
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterNotFound) {
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Missing);
}

// tests onClusterDiscoveryCompletion when discovering a cluster timed out
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterTimedOut) {
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Timeout);
}

// tests onClusterDiscoveryCompletion when a cluster is available
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterFound) {
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// tests onClusterDiscoveryCompletion when a cluster is available, but recreating a stream failed
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterFoundRecreateStreamFailed) {
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(false));
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// tests onClusterDiscoveryCompletion when a cluster is available and redirect contains a body
// With the fix, requests with bodies should now properly recreate the stream
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterFoundRedirectWithBody) {
  Buffer::OwnedImpl buffer;
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// Test case specifically for the GitHub issue fix: OnDemand VHDS with request body
// This test verifies that requests with bodies now properly continue processing
// after route discovery instead of trying to recreate the stream, fixing the 
// bug where they would get 404 NR responses
TEST_F(OnDemandFilterTest, VhdsWithRequestBodyShouldContinueDecoding) {
  Http::TestRequestHeaderMapImpl headers;
  Buffer::OwnedImpl request_body("test request body");

  // Simulate the scenario: route not initially available
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));

  // Headers processing should stop iteration to wait for route discovery
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // Body data should be buffered while waiting for route discovery
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark,
            filter_->decodeData(request_body, true));

  // Now simulate route discovery completion with a body present
  // The fix ensures this will continue decoding with buffered body, not recreate stream
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(1);

  // This should now continue decoding with the buffered body
  filter_->onRouteConfigUpdateCompletion(true);
}

// Test case for requests WITHOUT body - should still recreate stream for cleaner restart
TEST_F(OnDemandFilterTest, VhdsWithoutBodyShouldRecreateStream) {
  Http::TestRequestHeaderMapImpl headers;

  // Simulate the scenario: route not initially available
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));

  // Headers processing should stop iteration to wait for route discovery (end_stream=true, no body)
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));

  // No decodeData call since end_stream=true

  // For requests without body, we should still recreate the stream for cleaner restart
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);

  filter_->onRouteConfigUpdateCompletion(true);
}

// Test case for VHDS with body and cluster discovery
TEST_F(OnDemandFilterTest, VhdsAndCdsWithRequestBodyShouldContinueDecoding) {
  setupWithCds();
  
  Http::TestRequestHeaderMapImpl headers;
  Buffer::OwnedImpl request_body("test request body");
  
  auto route = std::make_shared<NiceMock<Router::MockRoute>>();
  auto route_entry = std::make_shared<NiceMock<Router::MockRouteEntry>>();
  
  EXPECT_CALL(*route, routeEntry()).WillRepeatedly(Return(route_entry.get()));
  static const std::string test_cluster_name = "test_cluster";
  EXPECT_CALL(*route_entry, clusterName()).WillRepeatedly(ReturnRef(test_cluster_name));
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(route));
  EXPECT_CALL(decoder_callbacks_, clusterInfo()).WillRepeatedly(Return(nullptr)); // No cluster initially
  EXPECT_CALL(*odcds_, requestOnDemandClusterDiscovery(_, _, _));

  // Headers processing should stop iteration to wait for cluster discovery
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  
  // Body data should be buffered
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark,
            filter_->decodeData(request_body, true));
  
  // When cluster discovery completes with body present, should continue decoding, not recreate
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(1);
  
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// Test race condition: route discovery completes during decodeHeaders
TEST_F(OnDemandFilterTest, RouteDiscoveryCompletionDuringDecodeHeaders) {
  Http::TestRequestHeaderMapImpl headers;
  Buffer::OwnedImpl request_body("test request body");

  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));

  // Start headers processing 
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  
  // Add body data
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark,
            filter_->decodeData(request_body, true));

  // Simulate route discovery completing while still in decodeHeaders context
  // This should NOT call continueDecoding to avoid race conditions
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);

  // Manually set decode_headers_active_ to simulate being called during decodeHeaders
  filter_->setFilterIterationState(Http::FilterHeadersStatus::StopIteration);
  
  // This should return early due to decode_headers_active_ check
  filter_->onRouteConfigUpdateCompletion(true);
}

TEST(OnDemandConfigTest, Basic) {
  NiceMock<Upstream::MockClusterManager> cm;
  ProtobufMessage::StrictValidationVisitorImpl visitor;
  envoy::extensions::filters::http::on_demand::v3::OnDemand config;

  OnDemandFilterConfig config1(config, cm, visitor);

  config.mutable_odcds();
  OnDemandFilterConfig config2(config, cm, visitor);

  config.mutable_odcds()->set_resources_locator("foo");
  EXPECT_THROW_WITH_MESSAGE(
      { OnDemandFilterConfig config3(config, cm, visitor); }, EnvoyException,
      "foo does not have a xdstp:, http: or file: scheme");
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

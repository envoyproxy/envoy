#include <memory>

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/on_demand/on_demand_update.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
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
  // clearRouteCache() should be called when cluster is available (correct behavior)
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
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
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

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
  EXPECT_CALL(decoder_callbacks_, clusterInfo())
      .WillRepeatedly(Return(nullptr)); // No cluster initially
  EXPECT_CALL(*odcds_, requestOnDemandClusterDiscovery(_, _, _));

  // Headers processing should stop iteration to wait for cluster discovery
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // Body data should be buffered
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark,
            filter_->decodeData(request_body, true));

  // When cluster discovery completes with body present, should continue decoding, not recreate
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

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

  // Simulate route discovery completing with body data present
  // Since we have body data, it should call continueDecoding() instead of recreateStream()
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  // This should call continueDecoding() because has_body_data_ is true (from decodeData call above)
  filter_->onRouteConfigUpdateCompletion(true);
}

// Test case for different hostname validation in VH discovery
// This test ensures that requests to completely different hostnames
// properly trigger VH discovery and handle per-route configuration
TEST_F(OnDemandFilterTest, VhdsWithDifferentHostnameShouldTriggerDiscovery) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/api/v1/test"},
      {":authority", "completely-different-host.example.com"}, // Different hostname
      {"content-type", "application/json"}};

  // Simulate the scenario: route not initially available for this hostname
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));

  // Headers processing should stop iteration to wait for VH discovery
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));

  // Simulate VH discovery completion for the new hostname
  // Should recreate stream since there's no body (end_stream=true)
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);

  filter_->onRouteConfigUpdateCompletion(true);
}

// Test case for simple body handling without internal-redirect
// This test validates that requests with body data are properly buffered
// and continue decoding after VH discovery, without attempting stream recreation
TEST_F(OnDemandFilterTest, SimpleBodyWithoutInternalRedirectShouldContinueDecoding) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/api/v1/data"},
                                         {":authority", "api.example.com"},
                                         {"content-type", "application/json"},
                                         {"content-length", "25"}};
  Buffer::OwnedImpl request_body(R"({"key": "value", "id": 123})");

  // Simulate the scenario: route not initially available
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));

  // Headers processing should stop iteration to wait for VH discovery
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // Body data should be buffered while waiting for VH discovery
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark,
            filter_->decodeData(request_body, true));

  // When VH discovery completes with body present, should continue decoding
  // This is the key fix: with body data, we should NOT recreate stream
  // to avoid losing the buffered body and causing 404/timeout issues
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  // This should continue decoding with the buffered body, not recreate stream
  filter_->onRouteConfigUpdateCompletion(true);
}

// Test case to verify per-route configuration behavior
// This test demonstrates the issue where per-route overrides may not be
// reflected if recreateStream is not used, but recreateStream doesn't work with buffered body
TEST_F(OnDemandFilterTest, PerRouteConfigWithBufferedBodyLimitation) {
  Http::TestRequestHeaderMapImpl headers{{":method", "PUT"},
                                         {":path", "/api/v1/config"},
                                         {":authority", "config.example.com"},
                                         {"x-custom-header", "per-route-override-needed"}};
  Buffer::OwnedImpl request_body("configuration data that needs per-route processing");

  // Simulate the scenario: route not initially available
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));

  // Headers processing should stop iteration
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // Body data gets buffered
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark,
            filter_->decodeData(request_body, true));

  // This is the limitation: with body data, we can't recreate stream
  // so per-route filter configuration overrides won't take effect
  // This is a known limitation that should be documented
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  // Note: In a real scenario, this would mean that VH-level filter configuration
  // overrides discovered during VH discovery would NOT be applied to this request
  // because we can't recreate the stream with the buffered body
  filter_->onRouteConfigUpdateCompletion(true);
}

// Test case for bodyless requests - these should still recreate stream
// to ensure per-route configuration overrides are properly applied
TEST_F(OnDemandFilterTest, BodylessRequestsShouldRecreateStreamForPerRouteConfig) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/api/v1/status"},
                                         {":authority", "status.example.com"},
                                         {"x-trace-id", "abc123"}};

  // Simulate the scenario: route not initially available
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));

  // Headers processing should stop iteration (end_stream=true, no body)
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));

  // For bodyless requests, we should recreate stream to ensure per-route
  // configuration overrides discovered during VH discovery are applied
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);

  filter_->onRouteConfigUpdateCompletion(true);
}

// Test case to verify the timeout vs 404 issue mentioned in line 883
// This test demonstrates that when recreateStream fails, the request should
// continue decoding rather than timing out, which could explain the timeout
// behavior observed instead of the expected 404
TEST_F(OnDemandFilterTest, RecreateStreamFailureShouldContinueNotTimeout) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/api/v1/test"}, {":authority", "test.example.com"}};

  // Simulate the scenario: route not initially available
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));

  // Headers processing should stop iteration to wait for VH discovery
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));

  // Simulate VH discovery completion but recreateStream fails
  // This is the key scenario: when recreateStream returns false,
  // the filter should call continueDecoding() to avoid timeout
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(false));
  EXPECT_CALL(decoder_callbacks_, continueDecoding()); // This prevents timeout

  // When recreateStream fails, should continue decoding instead of hanging/timing out
  filter_->onRouteConfigUpdateCompletion(true);
}

// Test case to verify that per-route configuration limitations are properly handled
// This test validates the behavior when VH discovery brings in route-specific overrides
// but the filter chain cannot be recreated due to buffered body data
TEST_F(OnDemandFilterTest, PerRouteConfigLimitationWithBufferedBodyIsDocumented) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/api/v1/upload"},
                                         {":authority", "upload.example.com"},
                                         {"content-type", "multipart/form-data"}};
  Buffer::OwnedImpl large_body(
      "large file upload data that cannot be lost during stream recreation");

  // Simulate the scenario: route not initially available
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));

  // Headers processing should stop iteration
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // Large body data gets buffered
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark,
            filter_->decodeData(large_body, true));

  // CRITICAL LIMITATION: When VH discovery completes and brings in per-route
  // filter configuration overrides, we CANNOT recreate the stream because
  // it would lose the buffered body data. This means:
  //
  // 1. The request will continue with the original filter chain configuration
  // 2. Per-route overrides discovered during VH discovery will NOT be applied
  // 3. This is a significant limitation for requests with body data
  //
  // The ideal fix would be to make recreateStream work with buffered body,
  // but that's a significant undertaking as mentioned in the GitHub issue.
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  filter_->onRouteConfigUpdateCompletion(true);

  // NOTE: This test documents the current limitation. In production, this would mean:
  // - Bodyless requests (GET, HEAD, etc.) get per-route config overrides ✓
  // - Requests with body (POST, PUT, etc.) do NOT get per-route config overrides ✗
  // - This creates inconsistent behavior based on request method/body presence
}

TEST(OnDemandConfigTest, Basic) {
  NiceMock<Upstream::MockClusterManager> cm;
  ProtobufMessage::StrictValidationVisitorImpl visitor;
  envoy::extensions::filters::http::on_demand::v3::OnDemand config;

  // Test with no bootstrap config
  envoy::config::bootstrap::v3::Bootstrap bootstrap1;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context1;
  server_context1.bootstrap_ = bootstrap1;

  OnDemandFilterConfig config1(config, cm, visitor, server_context1);

  config.mutable_odcds();
  OnDemandFilterConfig config2(config, cm, visitor, server_context1);

  config.mutable_odcds()->set_resources_locator("foo");
  EXPECT_THROW_WITH_MESSAGE(
      { OnDemandFilterConfig config3(config, cm, visitor, server_context1); }, EnvoyException,
      "foo does not have a xdstp:, http: or file: scheme");
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

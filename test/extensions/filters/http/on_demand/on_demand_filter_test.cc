#include <memory>

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/on_demand/on_demand_update.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

using StatusHelpers::HasStatus;
using StatusHelpers::IsOk;

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
  EXPECT_CALL(decoder_callbacks_, clusterInfo())
      .WillOnce(Return(OptRef<const Upstream::ClusterInfo>{}));
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
  EXPECT_CALL(decoder_callbacks_, clusterInfo())
      .WillOnce(Return(OptRef<const Upstream::ClusterInfo>{}));
  EXPECT_CALL(*odcds_, requestOnDemandClusterDiscovery(_, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteAvailableButClusterNameIsEmpty) {
  setupWithCds();
  Http::TestRequestHeaderMapImpl headers;
  std::string empty_cluster_name;
  EXPECT_CALL(decoder_callbacks_, clusterInfo())
      .WillOnce(Return(OptRef<const Upstream::ClusterInfo>{}));
  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, clusterName())
      .WillOnce(ReturnRef(empty_cluster_name));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteIsNotAvailableAndOdCdsIsEnabled) {
  setupWithCds();
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(OptRef<const Router::Route>{}));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
}

TEST_F(OnDemandFilterTest, TestDecodeHeadersWhenRouteIsNotAvailable) {
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(OptRef<const Router::Route>{}));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
}

// tests onRouteConfigUpdateCompletion() invoked synchronously while decodeHeaders() is still
// active: the callback must not continue decoding (decode_headers_active_ == true).
TEST_F(OnDemandFilterTest, TestDecodeHeadersRouteConfigUpdateCompletesSynchronously) {
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(OptRef<const Router::Route>{}));
  // Invoke the route config update callback synchronously, before requestRouteConfigUpdate()
  // returns, i.e. while decode_headers_active_ is still true.
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_))
      .WillOnce(Invoke(
          [](Http::RouteConfigUpdatedCallbackSharedPtr callback) -> void { (*callback)(true); }));
  // continueDecoding() must not be called from within decodeHeaders().
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  filter_->decodeHeaders(headers, true);
}

TEST_F(OnDemandFilterTest, TestDecodeTrailers) {
  Http::TestRequestTrailerMapImpl headers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

// tests onRouteConfigUpdateCompletion() when redirect contains a body with trailers (fully read)
TEST_F(OnDemandFilterTest, OnRouteConfigUpdateCompletionRestartsActiveStreamWithTrailers) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_vhds_no_recreate_stream", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  Http::TestRequestTrailerMapImpl trailers;
  Buffer::OwnedImpl buffer;
  // Simulate request with body and trailers (end_stream = true)
  filter_->decodeHeaders(headers, false);
  filter_->decodeData(buffer, false);
  filter_->decodeTrailers(trailers);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onClusterDiscoveryCompletion() when redirect contains a body with trailers (fully read)
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterFoundWithTrailers) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_cluster_no_recreate_stream", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  Http::TestRequestTrailerMapImpl trailers;
  Buffer::OwnedImpl buffer;
  // Simulate request with body and trailers (end_stream = true)
  filter_->decodeHeaders(headers, false);
  filter_->decodeData(buffer, false);
  filter_->decodeTrailers(trailers);
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// tests onClusterDiscoveryCompletion() when redirect contains a body with trailers (fully read)
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterFoundWithTrailersNoRecreateStream) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_cluster_no_recreate_stream", "true"}});
  Http::TestRequestHeaderMapImpl headers;
  Http::TestRequestTrailerMapImpl trailers;
  Buffer::OwnedImpl buffer;
  // Simulate request with body and trailers (end_stream = true)
  filter_->decodeHeaders(headers, false);
  filter_->decodeData(buffer, false);
  filter_->decodeTrailers(trailers);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
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

// tests onRouteConfigUpdateCompletion() when redirect contains a body but not fully read
TEST_F(OnDemandFilterTest,
       TestOnRouteConfigUpdateCompletionContinuesDecodingWithRedirectWithIncompleteBody) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_vhds_no_recreate_stream", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  Buffer::OwnedImpl buffer;
  // Simulate request with body that hasn't ended yet
  filter_->decodeHeaders(headers, false);
  filter_->decodeData(buffer, false);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onRouteConfigUpdateCompletion() when redirect contains a fully read body
TEST_F(OnDemandFilterTest, OnRouteConfigUpdateCompletionRestartsActiveStreamWithFullyReadBody) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_vhds_no_recreate_stream", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  Buffer::OwnedImpl buffer;
  // Simulate request with body that has been fully read (end_stream = true)
  filter_->decodeHeaders(headers, false);
  filter_->decodeData(buffer, true);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onRouteConfigUpdateCompletion() when ActiveStream recreation fails
TEST_F(OnDemandFilterTest, OnRouteConfigUpdateCompletionContinuesDecodingIfRedirectFails) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_vhds_no_recreate_stream", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  filter_->decodeHeaders(headers, true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(false));
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onRouteConfigUpdateCompletion() when route was resolved
TEST_F(OnDemandFilterTest, OnRouteConfigUpdateCompletionRestartsActiveStream) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_vhds_no_recreate_stream", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  filter_->decodeHeaders(headers, true);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onRouteConfigUpdateCompletion(true);
}

// tests onRouteConfigUpdateCompletion() with the no_recreate_stream guard ON (default).
// Verifies that continueDecoding is called instead of recreateStream.
TEST_F(OnDemandFilterTest, OnRouteConfigUpdateCompletionNoRecreateStream) {
  Http::TestRequestHeaderMapImpl headers;
  filter_->decodeHeaders(headers, true);
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, refreshRouteConfigSnapshot());
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  filter_->onRouteConfigUpdateCompletion(true);
}

// Pin the route-cache refresh policy on the guard-on path. When VHDS resolves
// the host (``route_exists == true``), the filter must call
// refreshRouteConfigSnapshot() then clearRouteCache() before continueDecoding()
// so the next route() lookup uses the post-VHDS ConfigImpl. Without this, the
// engaged-null cached_route_ short-circuits the router and the request 404s
// even though the vhost is now known.
TEST_F(OnDemandFilterTest,
       OnRouteConfigUpdateCompletionNoRecreateStreamClearsRouteCacheWhenRouteExists) {
  Http::TestRequestHeaderMapImpl headers;
  filter_->decodeHeaders(headers, true);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  {
    testing::InSequence s;
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, refreshRouteConfigSnapshot());
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    EXPECT_CALL(decoder_callbacks_, continueDecoding());
  }
  filter_->onRouteConfigUpdateCompletion(true);
}

// Counterpart of the above for the legitimate-miss case. When VHDS reports
// the vhost as unresolvable (``route_exists == false``), the filter must NOT
// refresh or clear the route cache: the engaged-null route is what makes the
// router emit a 404 immediately without a redundant lookup.
TEST_F(OnDemandFilterTest,
       OnRouteConfigUpdateCompletionNoRecreateStreamDoesNotClearRouteCacheWhenRouteDoesNotExist) {
  Http::TestRequestHeaderMapImpl headers;
  filter_->decodeHeaders(headers, true);
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, refreshRouteConfigSnapshot()).Times(0);
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  filter_->onRouteConfigUpdateCompletion(false);
}

// Regression for the combined on-demand VHDS + on-demand CDS scenario. With the
// no-recreate VHDS guard on (default) and OnDemandCds configured, a VHDS route
// resolution whose target cluster is absent must start on-demand CDS (returning
// StopIteration) instead of continuing straight to the router. This mirrors the
// legacy recreateStream() path, which re-entered decodeHeaders() and thereby
// reached the cluster-discovery step. Without the fix the guard-on path calls
// continueDecoding() directly and the cluster is never fetched.
TEST_F(OnDemandFilterTest, NoRecreateStreamVhdsResolutionTriggersOnDemandCds) {
  setupWithCds();
  Http::TestRequestHeaderMapImpl headers;

  // The route is unknown until VHDS resolves the virtual host, and resolved
  // thereafter.
  bool vhds_resolved = false;
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Invoke([&]() -> OptRef<const Router::Route> {
    return makeOptRefFromPtr<const Router::Route>(vhds_resolved ? decoder_callbacks_.route_.get()
                                                                : nullptr);
  }));

  // Initial decodeHeaders: route missing -> VHDS requested, StopIteration. CDS is
  // not reached yet because the route is still unknown.
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, requestRouteConfigUpdate(_));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));

  // VHDS resolves the host to a route whose target cluster is absent. The filter
  // must refresh the snapshot, clear the route cache, then start on-demand CDS and
  // hold the stream -- no continueDecoding() and no recreateStream().
  vhds_resolved = true;
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, refreshRouteConfigSnapshot());
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(decoder_callbacks_, clusterInfo())
      .WillOnce(Return(OptRef<const Upstream::ClusterInfo>{}));
  EXPECT_CALL(*odcds_, requestOnDemandClusterDiscovery(_, _, _));
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
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
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_cluster_no_recreate_stream", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  filter_->decodeHeaders(headers, true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// tests onClusterDiscoveryCompletion when a cluster is available
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterFoundNoRecreateStream) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_cluster_no_recreate_stream", "true"}});
  Http::TestRequestHeaderMapImpl headers;
  filter_->decodeHeaders(headers, true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// tests onClusterDiscoveryCompletion when a cluster is available with a fully read body
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterFoundWithFullyReadBody) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_cluster_no_recreate_stream", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  Buffer::OwnedImpl buffer;
  // Simulate request with body that has been fully read (end_stream = true)
  filter_->decodeHeaders(headers, false);
  filter_->decodeData(buffer, true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(true));
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// tests onClusterDiscoveryCompletion when a cluster is available with a fully read body
TEST_F(OnDemandFilterTest,
       OnClusterDiscoveryCompletionClusterFoundWithFullyReadBodyNoRecreateStream) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_cluster_no_recreate_stream", "true"}});
  Http::TestRequestHeaderMapImpl headers;
  Buffer::OwnedImpl buffer;
  // Simulate request with body that has been fully read (end_stream = true)
  filter_->decodeHeaders(headers, false);
  filter_->decodeData(buffer, true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// tests onClusterDiscoveryCompletion when a cluster is available, but recreating a stream failed
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterFoundRecreateStreamFailed) {
  TestScopedRuntime scoped_runtime;
  // This test is irrelevant for the case when there is no recreateStream call.
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_cluster_no_recreate_stream", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  filter_->decodeHeaders(headers, true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).WillOnce(Return(false));
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

// tests onClusterDiscoveryCompletion when a cluster is available, but redirect contains an
// incomplete body
TEST_F(OnDemandFilterTest, OnClusterDiscoveryCompletionClusterFoundRedirectWithIncompleteBody) {
  Http::TestRequestHeaderMapImpl headers;
  Buffer::OwnedImpl buffer;
  // Simulate request with body that hasn't ended yet
  filter_->decodeHeaders(headers, false);
  filter_->decodeData(buffer, false);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  filter_->onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus::Available);
}

TEST(OnDemandConfigTest, Basic) {
  NiceMock<Upstream::MockClusterManager> cm;
  ProtobufMessage::StrictValidationVisitorImpl visitor;
  envoy::extensions::filters::http::on_demand::v3::OnDemand config;

  absl::Status status1 = absl::OkStatus();
  OnDemandFilterConfig config1(config, cm, visitor, status1);
  EXPECT_THAT(status1, IsOk());

  config.mutable_odcds();
  absl::Status status2 = absl::OkStatus();
  OnDemandFilterConfig config2(config, cm, visitor, status2);
  EXPECT_THAT(status2, IsOk());

  config.mutable_odcds()->set_resources_locator("foo");
  absl::Status status3 = absl::OkStatus();
  OnDemandFilterConfig config3(config, cm, visitor, status3);
  EXPECT_THAT(status3, HasStatus(absl::StatusCode::kInvalidArgument,
                                 "foo does not have a xdstp:, http: or file: scheme"));

  // A valid xdstp resources_locator is decoded and an OdCds API is allocated with it.
  config.mutable_odcds()->set_resources_locator("xdstp://foo/envoy.config.cluster.v3.Cluster/bar");
  absl::Status status4 = absl::OkStatus();
  OnDemandFilterConfig config4(config, cm, visitor, status4);
  EXPECT_THAT(status4, IsOk());
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "common/buffer/buffer_impl.h"
#include "common/router/config_impl.h"
#include "common/router/router.h"
#include "common/router/upstream_request.h"

#include "extensions/common/proxy_protocol/proxy_protocol_header.h"

#include "test/common/http/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

class MockRouterFilterInterface : public RouterFilterInterface {
public:
  MockRouterFilterInterface()
      : config_("prefix.", context_, ShadowWriterPtr(new MockShadowWriter()), router_proto) {
    auto cluster_info = new NiceMock<Upstream::MockClusterInfo>();
    cluster_info->timeout_budget_stats_ = absl::nullopt;
    cluster_info_.reset(cluster_info);
    ON_CALL(*this, callbacks()).WillByDefault(Return(&callbacks_));
    ON_CALL(*this, config()).WillByDefault(ReturnRef(config_));
    ON_CALL(*this, cluster()).WillByDefault(Return(cluster_info_));
    ON_CALL(*this, upstreamRequests()).WillByDefault(ReturnRef(requests_));
    EXPECT_CALL(callbacks_.dispatcher_, setTrackedObject(_)).Times(AnyNumber());
    ON_CALL(*this, routeEntry()).WillByDefault(Return(&route_entry_));
    ON_CALL(callbacks_, connection()).WillByDefault(Return(&client_connection_));
    route_entry_.connect_config_.emplace(RouteEntry::ConnectConfig());
  }

  MOCK_METHOD(void, onUpstream100ContinueHeaders,
              (Http::ResponseHeaderMapPtr && headers, UpstreamRequest& upstream_request));
  MOCK_METHOD(void, onUpstreamHeaders,
              (uint64_t response_code, Http::ResponseHeaderMapPtr&& headers,
               UpstreamRequest& upstream_request, bool end_stream));
  MOCK_METHOD(void, onUpstreamData,
              (Buffer::Instance & data, UpstreamRequest& upstream_request, bool end_stream));
  MOCK_METHOD(void, onUpstreamTrailers,
              (Http::ResponseTrailerMapPtr && trailers, UpstreamRequest& upstream_request));
  MOCK_METHOD(void, onUpstreamMetadata, (Http::MetadataMapPtr && metadata_map));
  MOCK_METHOD(void, onUpstreamReset,
              (Http::StreamResetReason reset_reason, absl::string_view transport_failure,
               UpstreamRequest& upstream_request));
  MOCK_METHOD(void, onUpstreamHostSelected, (Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(void, onPerTryTimeout, (UpstreamRequest & upstream_request));
  MOCK_METHOD(void, onStreamMaxDurationReached, (UpstreamRequest & upstream_request));

  MOCK_METHOD(Http::StreamDecoderFilterCallbacks*, callbacks, ());
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, cluster, ());
  MOCK_METHOD(FilterConfig&, config, ());
  MOCK_METHOD(FilterUtility::TimeoutData, timeout, ());
  MOCK_METHOD(Http::RequestHeaderMap*, downstreamHeaders, ());
  MOCK_METHOD(Http::RequestTrailerMap*, downstreamTrailers, ());
  MOCK_METHOD(bool, downstreamResponseStarted, (), (const));
  MOCK_METHOD(bool, downstreamEndStream, (), (const));
  MOCK_METHOD(uint32_t, attemptCount, (), (const));
  MOCK_METHOD(const VirtualCluster*, requestVcluster, (), (const));
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));
  MOCK_METHOD(const std::list<UpstreamRequestPtr>&, upstreamRequests, (), (const));
  MOCK_METHOD(const UpstreamRequest*, finalUpstreamRequest, (), (const));
  MOCK_METHOD(TimeSource&, timeSource, ());

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<MockRouteEntry> route_entry_;
  NiceMock<Network::MockConnection> client_connection_;

  envoy::extensions::filters::http::router::v3::Router router_proto;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  FilterConfig config_;
  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  std::list<UpstreamRequestPtr> requests_;
};

} // namespace
} // namespace Router
} // namespace Envoy

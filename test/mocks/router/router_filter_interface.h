#pragma once

#include "source/common/router/router.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Router {

class MockRouterFilterInterface : public RouterFilterInterface {
public:
  MockRouterFilterInterface();
  ~MockRouterFilterInterface() override;

  MOCK_METHOD(void, onUpstream1xxHeaders,
              (Envoy::Http::ResponseHeaderMapPtr && headers, UpstreamRequest& upstream_request));
  MOCK_METHOD(void, onUpstreamHeaders,
              (uint64_t response_code, Envoy::Http::ResponseHeaderMapPtr&& headers,
               UpstreamRequest& upstream_request, bool end_stream));
  MOCK_METHOD(void, onUpstreamData,
              (Buffer::Instance & data, UpstreamRequest& upstream_request, bool end_stream));
  MOCK_METHOD(void, onUpstreamTrailers,
              (Envoy::Http::ResponseTrailerMapPtr && trailers, UpstreamRequest& upstream_request));
  MOCK_METHOD(void, onUpstreamMetadata, (Envoy::Http::MetadataMapPtr && metadata_map));
  MOCK_METHOD(void, onUpstreamReset,
              (Envoy::Http::StreamResetReason reset_reason, absl::string_view transport_failure,
               UpstreamRequest& upstream_request));
  MOCK_METHOD(void, onUpstreamHostSelected, (Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(void, onPerTryTimeout, (UpstreamRequest & upstream_request));
  MOCK_METHOD(void, onPerTryIdleTimeout, (UpstreamRequest & upstream_request));
  MOCK_METHOD(void, onStreamMaxDurationReached, (UpstreamRequest & upstream_request));

  MOCK_METHOD(Envoy::Http::StreamDecoderFilterCallbacks*, callbacks, ());
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, cluster, ());
  MOCK_METHOD(FilterConfig&, config, ());
  MOCK_METHOD(FilterUtility::TimeoutData, timeout, ());
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, dynamicMaxStreamDuration, (), (const));
  MOCK_METHOD(Envoy::Http::RequestHeaderMap*, downstreamHeaders, ());
  MOCK_METHOD(Envoy::Http::RequestTrailerMap*, downstreamTrailers, ());
  MOCK_METHOD(bool, downstreamResponseStarted, (), (const));
  MOCK_METHOD(bool, downstreamEndStream, (), (const));
  MOCK_METHOD(uint32_t, attemptCount, (), (const));
  MOCK_METHOD(const VirtualCluster*, requestVcluster, (), (const));
  MOCK_METHOD(const Route*, route, (), (const));
  MOCK_METHOD(const std::list<UpstreamRequestPtr>&, upstreamRequests, (), (const));
  MOCK_METHOD(const UpstreamRequest*, finalUpstreamRequest, (), (const));
  MOCK_METHOD(TimeSource&, timeSource, ());

  const RouteStatsContextOptRef routeStatsContext() const override {
    return RouteStatsContextOptRef();
  }

  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<MockRoute> route_;
  NiceMock<Network::MockConnection> client_connection_;

  envoy::extensions::filters::http::router::v3::Router router_proto;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::StatNamePool pool_;
  FilterConfig config_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_;
  std::list<UpstreamRequestPtr> requests_;
  Event::GlobalTimeSystem time_system_;
};

} // namespace Router
} // namespace Envoy

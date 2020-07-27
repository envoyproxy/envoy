#pragma once

#include "common/router/router.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Router {

class MockRouterFilterInterface : public RouterFilterInterface {
public:
  MockRouterFilterInterface();
  ~MockRouterFilterInterface() override;

  MOCK_METHOD(void, onUpstream100ContinueHeaders,
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
  MOCK_METHOD(void, onStreamMaxDurationReached, (UpstreamRequest & upstream_request));

  MOCK_METHOD(Envoy::Http::StreamDecoderFilterCallbacks*, callbacks, ());
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, cluster, ());
  MOCK_METHOD(FilterConfig&, config, ());
  MOCK_METHOD(FilterUtility::TimeoutData, timeout, ());
  MOCK_METHOD(Envoy::Http::RequestHeaderMap*, downstreamHeaders, ());
  MOCK_METHOD(Envoy::Http::RequestTrailerMap*, downstreamTrailers, ());
  MOCK_METHOD(bool, downstreamResponseStarted, (), (const));
  MOCK_METHOD(bool, downstreamEndStream, (), (const));
  MOCK_METHOD(uint32_t, attemptCount, (), (const));
  MOCK_METHOD(const VirtualCluster*, requestVcluster, (), (const));
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));
  MOCK_METHOD(const std::list<UpstreamRequestPtr>&, upstreamRequests, (), (const));
  MOCK_METHOD(const UpstreamRequest*, finalUpstreamRequest, (), (const));
  MOCK_METHOD(TimeSource&, timeSource, ());

  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<MockRouteEntry> route_entry_;
  NiceMock<Network::MockConnection> client_connection_;

  envoy::extensions::filters::http::router::v3::Router router_proto;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  FilterConfig config_;
  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  std::list<UpstreamRequestPtr> requests_;
};

} // namespace Router
} // namespace Envoy

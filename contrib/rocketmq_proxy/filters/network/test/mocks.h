#pragma once

#include "test/mocks/upstream/cluster_manager.h"

#include "contrib/rocketmq_proxy/filters/network/source/active_message.h"
#include "contrib/rocketmq_proxy/filters/network/source/conn_manager.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

namespace Router {
class MockRoute;
} // namespace Router

class MockActiveMessage : public ActiveMessage {
public:
  MockActiveMessage(ConnectionManager& conn_manager, RemotingCommandPtr&& request);
  ~MockActiveMessage() override;

  MOCK_METHOD(void, createFilterChain, ());
  MOCK_METHOD(void, sendRequestToUpstream, ());
  MOCK_METHOD(RemotingCommandPtr&, downstreamRequest, ());
  MOCK_METHOD(void, sendResponseToDownstream, (), (override));
  MOCK_METHOD(void, onQueryTopicRoute, ());
  MOCK_METHOD(void, onError, (absl::string_view), (override));
  MOCK_METHOD(ConnectionManager&, connectionManager, ());
  MOCK_METHOD(void, onReset, (), (override));
  MOCK_METHOD(bool, onUpstreamData,
              (Buffer::Instance&, bool, Tcp::ConnectionPool::ConnectionDataPtr&));
  MOCK_METHOD(MessageMetadataSharedPtr, metadata, (), (const, override));
  MOCK_METHOD(Router::RouteConstSharedPtr, route, (), (override));

  std::shared_ptr<Router::MockRoute> route_;
};

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig() override = default;

  MOCK_METHOD(RocketmqFilterStats&, stats, (), (override));
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, (), (override));
  MOCK_METHOD(Router::RouterPtr, createRouter, (), (override));
  MOCK_METHOD(bool, developMode, (), (const, override));
  MOCK_METHOD(std::string, proxyAddress, (), (override));
  MOCK_METHOD(Router::Config&, routerConfig, (), (override));

private:
  Stats::IsolatedStoreImpl store_;
  RocketmqFilterStats stats_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Router::RouterPtr router_;
};

namespace Router {

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry() override;

  // RocketmqProxy::Router::RouteEntry
  MOCK_METHOD(const std::string&, clusterName, (), (const, override));
  MOCK_METHOD(Envoy::Router::MetadataMatchCriteria*, metadataMatchCriteria, (), (const, override));

  std::string cluster_name_{"fake_cluster"};
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute() override;

  // RocketmqProxy::Router::Route
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const, override));

  NiceMock<MockRouteEntry> route_entry_;
};
} // namespace Router

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

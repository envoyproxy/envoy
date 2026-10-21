#pragma once

#include "envoy/upstream/thread_local_cluster.h"

#include "test/mocks/http/conn_pool.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/tcp/mocks.h"

#include "cluster_priority_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "load_balancer.h"

using ::testing::NiceMock;

namespace Envoy {
namespace Upstream {

class MockThreadLocalCluster : public ThreadLocalCluster {
public:
  MockThreadLocalCluster();
  ~MockThreadLocalCluster() override;

  Host::CreateConnectionData tcpConn(LoadBalancerContext* context) override {
    MockHost::MockCreateConnectionData data = tcpConn_(context);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  // Upstream::ThreadLocalCluster
  MOCK_METHOD(const PrioritySet&, prioritySet, (), (override));
  MOCK_METHOD(ClusterInfoConstSharedPtr, info, (), (override));
  MOCK_METHOD(LoadBalancer&, loadBalancer, (), (override));
  MOCK_METHOD(std::optional<HttpPoolData>, httpConnPool,
              (HostConstSharedPtr host, ResourcePriority priority,
               std::optional<Http::Protocol> downstream_protocol, LoadBalancerContext* context),
              (override));
  MOCK_METHOD(HostSelectionResponse, chooseHost, (LoadBalancerContext * context), (override));
  MOCK_METHOD(std::optional<TcpPoolData>, tcpConnPool,
              (HostConstSharedPtr host, ResourcePriority priority, LoadBalancerContext* context),
              (override));
  MOCK_METHOD(std::optional<TcpPoolData>, tcpConnPool,
              (ResourcePriority priority, LoadBalancerContext* context), (override));
  MOCK_METHOD(MockHost::MockCreateConnectionData, tcpConn_, (LoadBalancerContext * context));
  MOCK_METHOD(Http::AsyncClient&, httpAsyncClient, (), (override));
  MOCK_METHOD(Tcp::AsyncTcpClientPtr, tcpAsyncClient,
              (LoadBalancerContext * context, Tcp::AsyncTcpClientOptionsConstSharedPtr options),
              (override));
  MOCK_METHOD(UnitFloat, dropOverload, (), (const, override));
  MOCK_METHOD(const std::string&, dropCategory, (), (const, override));
  MOCK_METHOD(void, setDropOverload, (UnitFloat), (override));
  MOCK_METHOD(void, setDropCategory, (absl::string_view), (override));

  NiceMock<MockClusterMockPrioritySet> cluster_;
  NiceMock<MockLoadBalancer> lb_;
  NiceMock<Http::ConnectionPool::MockInstance> conn_pool_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Tcp::ConnectionPool::MockInstance> tcp_conn_pool_;
};

} // namespace Upstream
} // namespace Envoy

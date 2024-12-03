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
  MOCK_METHOD(const PrioritySet&, prioritySet, ());
  MOCK_METHOD(ClusterInfoConstSharedPtr, info, ());
  MOCK_METHOD(LoadBalancer&, loadBalancer, ());
  MOCK_METHOD(absl::optional<HttpPoolData>, httpConnPool,
              (ResourcePriority priority, absl::optional<Http::Protocol> downstream_protocol,
               LoadBalancerContext* context));
  MOCK_METHOD(absl::optional<TcpPoolData>, tcpConnPool,
              (ResourcePriority priority, LoadBalancerContext* context));
  MOCK_METHOD(MockHost::MockCreateConnectionData, tcpConn_, (LoadBalancerContext * context));
  MOCK_METHOD(Http::AsyncClient&, httpAsyncClient, ());
  MOCK_METHOD(Tcp::AsyncTcpClientPtr, tcpAsyncClient,
              (LoadBalancerContext * context, Tcp::AsyncTcpClientOptionsConstSharedPtr options));
  MOCK_METHOD(UnitFloat, dropOverload, (), (const));
  MOCK_METHOD(const std::string&, dropCategory, (), (const));
  MOCK_METHOD(void, setDropOverload, (UnitFloat));
  MOCK_METHOD(void, setDropCategory, (absl::string_view));

  NiceMock<MockClusterMockPrioritySet> cluster_;
  NiceMock<MockLoadBalancer> lb_;
  NiceMock<Http::ConnectionPool::MockInstance> conn_pool_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Tcp::ConnectionPool::MockInstance> tcp_conn_pool_;
};

} // namespace Upstream
} // namespace Envoy

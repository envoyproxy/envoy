#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/tcp/mocks.h"

#include "cluster_manager_factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "thread_local_cluster.h"

namespace Envoy {
namespace Upstream {
using ::testing::NiceMock;
class MockClusterManager : public ClusterManager {
public:
  explicit MockClusterManager(TimeSource& time_source);
  MockClusterManager();
  ~MockClusterManager() override;

  ClusterUpdateCallbacksHandlePtr
  addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks& callbacks) override {
    return ClusterUpdateCallbacksHandlePtr{addThreadLocalClusterUpdateCallbacks_(callbacks)};
  }

  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster,
                                               LoadBalancerContext* context) override {
    MockHost::MockCreateConnectionData data = tcpConnForCluster_(cluster, context);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  ClusterManagerFactory& clusterManagerFactory() override { return cluster_manager_factory_; }

  // Upstream::ClusterManager
  MOCK_METHOD(bool, addOrUpdateCluster,
              (const envoy::config::cluster::v3::Cluster& cluster,
               const std::string& version_info));
  MOCK_METHOD(void, setPrimaryClustersInitializedCb, (PrimaryClustersReadyCallback));
  MOCK_METHOD(void, setInitializedCb, (InitializationCompleteCallback));
  MOCK_METHOD(void, initializeSecondaryClusters,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap));
  MOCK_METHOD(ClusterInfoMap, clusters, ());
  MOCK_METHOD(const ClusterSet&, primaryClusters, ());
  MOCK_METHOD(ThreadLocalCluster*, get, (absl::string_view cluster));
  MOCK_METHOD(Http::ConnectionPool::Instance*, httpConnPoolForCluster,
              (const std::string& cluster, ResourcePriority priority,
               absl::optional<Http::Protocol> downstream_protocol, LoadBalancerContext* context));
  MOCK_METHOD(Tcp::ConnectionPool::Instance*, tcpConnPoolForCluster,
              (const std::string& cluster, ResourcePriority priority,
               LoadBalancerContext* context));
  MOCK_METHOD(MockHost::MockCreateConnectionData, tcpConnForCluster_,
              (const std::string& cluster, LoadBalancerContext* context));
  MOCK_METHOD(Http::AsyncClient&, httpAsyncClientForCluster, (const std::string& cluster));
  MOCK_METHOD(bool, removeCluster, (const std::string& cluster));
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(const envoy::config::core::v3::BindConfig&, bindConfig, (), (const));
  MOCK_METHOD(Config::GrpcMuxSharedPtr, adsMux, ());
  MOCK_METHOD(Grpc::AsyncClientManager&, grpcAsyncClientManager, ());
  MOCK_METHOD(const std::string, versionInfo, (), (const));
  MOCK_METHOD(const absl::optional<std::string>&, localClusterName, (), (const));
  MOCK_METHOD(ClusterUpdateCallbacksHandle*, addThreadLocalClusterUpdateCallbacks_,
              (ClusterUpdateCallbacks & callbacks));
  MOCK_METHOD(Config::SubscriptionFactory&, subscriptionFactory, ());

  NiceMock<Http::ConnectionPool::MockInstance> conn_pool_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Tcp::ConnectionPool::MockInstance> tcp_conn_pool_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  envoy::config::core::v3::BindConfig bind_config_;
  std::shared_ptr<NiceMock<Config::MockGrpcMux>> ads_mux_;
  NiceMock<Grpc::MockAsyncClientManager> async_client_manager_;
  absl::optional<std::string> local_cluster_name_;
  NiceMock<MockClusterManagerFactory> cluster_manager_factory_;
  NiceMock<Config::MockSubscriptionFactory> subscription_factory_;
};
} // namespace Upstream

} // namespace Envoy

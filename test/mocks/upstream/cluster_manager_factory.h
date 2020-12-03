#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "common/upstream/stat_names.h"

#include "test/mocks/secret/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
using ::testing::NiceMock;

class MockClusterManagerFactory : public ClusterManagerFactory {
public:
  MockClusterManagerFactory();
  ~MockClusterManagerFactory() override;

  Secret::MockSecretManager& secretManager() override { return secret_manager_; };

  MOCK_METHOD(ClusterManagerPtr, clusterManagerFromProto,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap));

  MOCK_METHOD(Http::ConnectionPool::InstancePtr, allocateConnPool,
              (Event::Dispatcher & dispatcher, HostConstSharedPtr host, ResourcePriority priority,
               Http::Protocol protocol, const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
               ClusterConnectivityState& state));

  MOCK_METHOD(Tcp::ConnectionPool::InstancePtr, allocateTcpConnPool,
              (Event::Dispatcher & dispatcher, HostConstSharedPtr host, ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               Network::TransportSocketOptionsSharedPtr, ClusterConnectivityState& state));

  MOCK_METHOD((std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>), clusterFromProto,
              (const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
               Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api));

  MOCK_METHOD(CdsApiPtr, createCds,
              (const envoy::config::core::v3::ConfigSource& cds_config, ClusterManager& cm));

  const UpstreamStatNames& upstreamStatNames() const override { return upstream_stat_names_; }

private:
  NiceMock<Secret::MockSecretManager> secret_manager_;
  Stats::TestSymbolTable symbol_table_;
  UpstreamStatNames upstream_stat_names_;
};

} // namespace Upstream
} // namespace Envoy

#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "source/common/quic/envoy_quic_network_observer_registry_factory.h"
#include "source/common/singleton/manager_impl.h"

#include "test/mocks/secret/mocks.h"
#include "test/test_common/thread_factory_for_test.h"

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
  Singleton::Manager& singletonManager() override { return singleton_manager_; }

  MOCK_METHOD(ClusterManagerPtr, clusterManagerFromProto,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap));

  MOCK_METHOD(Http::ConnectionPool::InstancePtr, allocateConnPool,
              (Event::Dispatcher & dispatcher, HostConstSharedPtr host, ResourcePriority priority,
               std::vector<Http::Protocol>& protocol,
               const absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>&
                   alternate_protocol_options,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
               TimeSource& source, ClusterConnectivityState& state,
               Http::PersistentQuicInfoPtr& quic_info,
               OptRef<Quic::EnvoyQuicNetworkObserverRegistry> network_observer_registry));

  MOCK_METHOD(Tcp::ConnectionPool::InstancePtr, allocateTcpConnPool,
              (Event::Dispatcher & dispatcher, HostConstSharedPtr host, ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               Network::TransportSocketOptionsConstSharedPtr, ClusterConnectivityState& state,
               absl::optional<std::chrono::milliseconds> tcp_pool_idle_timeout));

  MOCK_METHOD((absl::StatusOr<std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>>),
              clusterFromProto,
              (const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
               Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api));

  MOCK_METHOD(CdsApiPtr, createCds,
              (const envoy::config::core::v3::ConfigSource& cds_config,
               const xds::core::v3::ResourceLocator* cds_resources_locator, ClusterManager& cm));

private:
  NiceMock<Secret::MockSecretManager> secret_manager_;
  Singleton::ManagerImpl singleton_manager_;
};
} // namespace Upstream
} // namespace Envoy

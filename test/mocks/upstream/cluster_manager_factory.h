#pragma once

#include "envoy/certificate_provider/certificate_provider_manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/singleton/manager_impl.h"

#include "test/mocks/secret/mocks.h"
#include "test/test_common/thread_factory_for_test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::NiceMock;
namespace Envoy {
namespace CertificateProvider {
class MockCertificateProviderManager : public CertificateProviderManager {
public:
  MockCertificateProviderManager();
  ~MockCertificateProviderManager() override;

  MOCK_METHOD(void, addCertificateProvider,
              (absl::string_view name, const envoy::config::core::v3::TypedExtensionConfig& config,
               Server::Configuration::TransportSocketFactoryContext& factory_context));

  MOCK_METHOD(CertificateProviderSharedPtr, getCertificateProvider, (absl::string_view name));
};
} // namespace CertificateProvider

namespace Upstream {
class MockClusterManagerFactory : public ClusterManagerFactory {
public:
  MockClusterManagerFactory();
  ~MockClusterManagerFactory() override;

  Secret::MockSecretManager& secretManager() override { return secret_manager_; };
  Singleton::Manager& singletonManager() override { return singleton_manager_; }
  CertificateProvider::CertificateProviderManager& certificateProviderManager() override {
    return certificate_provider_manager_;
  }

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
               Http::PersistentQuicInfoPtr& quic_info));

  MOCK_METHOD(Tcp::ConnectionPool::InstancePtr, allocateTcpConnPool,
              (Event::Dispatcher & dispatcher, HostConstSharedPtr host, ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               Network::TransportSocketOptionsConstSharedPtr, ClusterConnectivityState& state));

  MOCK_METHOD((std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>), clusterFromProto,
              (const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
               Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api));

  MOCK_METHOD(CdsApiPtr, createCds,
              (const envoy::config::core::v3::ConfigSource& cds_config,
               const xds::core::v3::ResourceLocator* cds_resources_locator, ClusterManager& cm));

private:
  NiceMock<Secret::MockSecretManager> secret_manager_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<CertificateProvider::MockCertificateProviderManager> certificate_provider_manager_;
};
} // namespace Upstream
} // namespace Envoy

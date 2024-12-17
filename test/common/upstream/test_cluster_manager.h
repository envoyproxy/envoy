#pragma once

#include <memory>
#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/network/listen_socket.h"
#include "envoy/upstream/upstream.h"

#include "source/common/api/api_impl.h"
#include "source/common/config/utility.h"
#include "source/common/http/context_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/cluster_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/common/upstream/utility.h"
#include "test/integration/clusters/custom_static_cluster.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/config/xds_manager.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

// The tests in this file are split between testing with real clusters and some with mock clusters.
// By default we setup to call the real cluster creation function. Individual tests can override
// the expectations when needed.
class TestClusterManagerFactory : public ClusterManagerFactory {
public:
  TestClusterManagerFactory() : api_(Api::createApiForTest(stats_, random_)) {

    ON_CALL(server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(*this, clusterFromProto_(_, _, _, _))
        .WillByDefault(Invoke(
            [&](const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
                Outlier::EventLoggerSharedPtr outlier_event_logger,
                bool added_via_api) -> std::pair<ClusterSharedPtr, ThreadAwareLoadBalancer*> {
              auto result = ClusterFactoryImplBase::create(
                  cluster, server_context_, cm,
                  [this]() -> Network::DnsResolverSharedPtr { return this->dns_resolver_; },
                  ssl_context_manager_, outlier_event_logger, added_via_api);
              // Convert from load balancer unique_ptr -> raw pointer -> unique_ptr.
              if (!result.ok()) {
                throw EnvoyException(std::string(result.status().message()));
              }
              return std::make_pair(result->first, result->second.release());
            }));
    ON_CALL(server_context_, singletonManager()).WillByDefault(ReturnRef(singleton_manager_));
  }

  ~TestClusterManagerFactory() override { dispatcher_.to_delete_.clear(); }

  Http::ConnectionPool::InstancePtr allocateConnPool(
      Event::Dispatcher&, HostConstSharedPtr host, ResourcePriority, std::vector<Http::Protocol>&,
      const absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>&
          alternate_protocol_options,
      const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options, TimeSource&,
      ClusterConnectivityState& state, Http::PersistentQuicInfoPtr& /*quic_info*/,
      OptRef<Quic::EnvoyQuicNetworkObserverRegistry> network_observer_registry) override {
    return Http::ConnectionPool::InstancePtr{allocateConnPool_(host, alternate_protocol_options,
                                                               options, transport_socket_options,
                                                               state, network_observer_registry)};
  }

  Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher&, HostConstSharedPtr host, ResourcePriority,
                      const Network::ConnectionSocket::OptionsSharedPtr&,
                      Network::TransportSocketOptionsConstSharedPtr,
                      Upstream::ClusterConnectivityState&,
                      absl::optional<std::chrono::milliseconds>) override {
    return Tcp::ConnectionPool::InstancePtr{allocateTcpConnPool_(host)};
  }

  absl::StatusOr<std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>>
  clusterFromProto(const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
                   Outlier::EventLoggerSharedPtr outlier_event_logger,
                   bool added_via_api) override {
    auto result = clusterFromProto_(cluster, cm, outlier_event_logger, added_via_api);
    return std::make_pair(result.first, ThreadAwareLoadBalancerPtr(result.second));
  }

  absl::StatusOr<CdsApiPtr> createCds(const envoy::config::core::v3::ConfigSource&,
                                      const xds::core::v3::ResourceLocator*,
                                      ClusterManager&) override {
    return CdsApiPtr{createCds_()};
  }

  absl::StatusOr<ClusterManagerPtr>
  clusterManagerFromProto(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) override {
    return ClusterManagerPtr{clusterManagerFromProto_(bootstrap)};
  }

  Secret::SecretManager& secretManager() override { return secret_manager_; }
  Singleton::Manager& singletonManager() override { return singleton_manager_; }

  MOCK_METHOD(ClusterManager*, clusterManagerFromProto_,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap));
  MOCK_METHOD(Http::ConnectionPool::Instance*, allocateConnPool_,
              (HostConstSharedPtr host,
               const absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>&
                   alternate_protocol_options,
               Network::ConnectionSocket::OptionsSharedPtr,
               Network::TransportSocketOptionsConstSharedPtr, ClusterConnectivityState&,
               OptRef<Quic::EnvoyQuicNetworkObserverRegistry> network_observer_registry));
  MOCK_METHOD(Tcp::ConnectionPool::Instance*, allocateTcpConnPool_, (HostConstSharedPtr host));
  MOCK_METHOD((std::pair<ClusterSharedPtr, ThreadAwareLoadBalancer*>), clusterFromProto_,
              (const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
               Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api));
  MOCK_METHOD(CdsApi*, createCds_, ());

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;
  NiceMock<ThreadLocal::MockInstance>& tls_ = server_context_.thread_local_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  NiceMock<Runtime::MockLoader>& runtime_ = server_context_.runtime_loader_;
  NiceMock<Event::MockDispatcher>& dispatcher_ = server_context_.dispatcher_;
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager_{server_context_};
  NiceMock<LocalInfo::MockLocalInfo>& local_info_ = server_context_.local_info_;
  NiceMock<Server::MockAdmin>& admin_ = server_context_.admin_;
  NiceMock<Secret::MockSecretManager> secret_manager_;
  NiceMock<AccessLog::MockAccessLogManager>& log_manager_ = server_context_.access_log_manager_;
  Singleton::ManagerImpl singleton_manager_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_;
  Server::MockOptions& options_ = server_context_.options_;
};

// A test version of ClusterManagerImpl that provides a way to get a non-const handle to the
// clusters, which is necessary in order to call updateHosts on the priority set.
class TestClusterManagerImpl : public ClusterManagerImpl {
public:
  static std::unique_ptr<TestClusterManagerImpl> createAndInit(
      const envoy::config::bootstrap::v3::Bootstrap& bootstrap, ClusterManagerFactory& factory,
      Server::Configuration::CommonFactoryContext& context, Stats::Store& stats,
      ThreadLocal::Instance& tls, Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info,
      AccessLog::AccessLogManager& log_manager, Event::Dispatcher& main_thread_dispatcher,
      Server::Admin& admin, ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
      Http::Context& http_context, Grpc::Context& grpc_context, Router::Context& router_context,
      Server::Instance& server, Config::XdsManager& xds_manager) {
    absl::Status creation_status = absl::OkStatus();
    auto cluster_manager = std::unique_ptr<TestClusterManagerImpl>{new TestClusterManagerImpl(
        bootstrap, factory, context, stats, tls, runtime, local_info, log_manager,
        main_thread_dispatcher, admin, validation_context, api, http_context, grpc_context,
        router_context, server, xds_manager, creation_status)};
    THROW_IF_NOT_OK(creation_status);
    THROW_IF_NOT_OK(cluster_manager->initialize(bootstrap));
    return cluster_manager;
  }

  std::map<std::string, std::reference_wrapper<Cluster>> activeClusters() {
    std::map<std::string, std::reference_wrapper<Cluster>> clusters;
    for (auto& cluster : active_clusters_) {
      clusters.emplace(cluster.first, *cluster.second->cluster_);
    }
    return clusters;
  }

  const ClusterInitializationMap& clusterInitializationMap() const {
    return cluster_initialization_map_;
  }

  OdCdsApiHandlePtr createOdCdsApiHandle(OdCdsApiSharedPtr odcds) {
    return ClusterManagerImpl::OdCdsApiHandleImpl::create(*this, std::move(odcds));
  }

  void notifyExpiredDiscovery(absl::string_view name) {
    ClusterManagerImpl::notifyExpiredDiscovery(name);
  }

  ClusterDiscoveryManager createAndSwapClusterDiscoveryManager(std::string thread_name) {
    return ClusterManagerImpl::createAndSwapClusterDiscoveryManager(std::move(thread_name));
  }

protected:
  using ClusterManagerImpl::ClusterManagerImpl;

  TestClusterManagerImpl(
      const envoy::config::bootstrap::v3::Bootstrap& bootstrap, ClusterManagerFactory& factory,
      Server::Configuration::CommonFactoryContext& context, Stats::Store& stats,
      ThreadLocal::Instance& tls, Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info,
      AccessLog::AccessLogManager& log_manager, Event::Dispatcher& main_thread_dispatcher,
      Server::Admin& admin, ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
      Http::Context& http_context, Grpc::Context& grpc_context, Router::Context& router_context,
      Server::Instance& server, Config::XdsManager& xds_manager, absl::Status& creation_status)
      : ClusterManagerImpl(bootstrap, factory, context, stats, tls, runtime, local_info,
                           log_manager, main_thread_dispatcher, admin, validation_context, api,
                           http_context, grpc_context, router_context, server, xds_manager,
                           creation_status) {}
};

} // namespace Upstream
} // namespace Envoy

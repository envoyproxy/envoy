#pragma once

#include <memory>
#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/network/listen_socket.h"
#include "envoy/upstream/upstream.h"

#include "common/api/api_impl.h"
#include "common/config/utility.h"
#include "common/http/context_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/cluster_manager_impl.h"
#include "common/upstream/subset_lb.h"

#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/common/upstream/utility.h"
#include "test/integration/clusters/custom_static_cluster.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
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

namespace Envoy {
namespace Upstream {

// The tests in this file are split between testing with real clusters and some with mock clusters.
// By default we setup to call the real cluster creation function. Individual tests can override
// the expectations when needed.
class TestClusterManagerFactory : public ClusterManagerFactory {
public:
  TestClusterManagerFactory() : api_(Api::createApiForTest(stats_, random_)) {
    ON_CALL(*this, clusterFromProto_(_, _, _, _))
        .WillByDefault(Invoke(
            [&](const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
                Outlier::EventLoggerSharedPtr outlier_event_logger,
                bool added_via_api) -> std::pair<ClusterSharedPtr, ThreadAwareLoadBalancer*> {
              auto result = ClusterFactoryImplBase::create(
                  cluster, cm, stats_, tls_, dns_resolver_, ssl_context_manager_, runtime_,
                  dispatcher_, log_manager_, local_info_, admin_, singleton_manager_,
                  outlier_event_logger, added_via_api, validation_visitor_, *api_);
              // Convert from load balancer unique_ptr -> raw pointer -> unique_ptr.
              return std::make_pair(result.first, result.second.release());
            }));
  }

  Http::ConnectionPool::InstancePtr allocateConnPool(
      Event::Dispatcher&, HostConstSharedPtr host, ResourcePriority, Http::Protocol,
      const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsSharedPtr& transport_socket_options) override {
    return Http::ConnectionPool::InstancePtr{
        allocateConnPool_(host, options, transport_socket_options)};
  }

  Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher&, HostConstSharedPtr host, ResourcePriority,
                      const Network::ConnectionSocket::OptionsSharedPtr&,
                      Network::TransportSocketOptionsSharedPtr) override {
    return Tcp::ConnectionPool::InstancePtr{allocateTcpConnPool_(host)};
  }

  std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
  clusterFromProto(const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
                   Outlier::EventLoggerSharedPtr outlier_event_logger,
                   bool added_via_api) override {
    auto result = clusterFromProto_(cluster, cm, outlier_event_logger, added_via_api);
    return std::make_pair(result.first, ThreadAwareLoadBalancerPtr(result.second));
  }

  CdsApiPtr createCds(const envoy::config::core::v3::ConfigSource&, ClusterManager&) override {
    return CdsApiPtr{createCds_()};
  }

  ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) override {
    return ClusterManagerPtr{clusterManagerFromProto_(bootstrap)};
  }

  Secret::SecretManager& secretManager() override { return secret_manager_; }

  MOCK_METHOD(ClusterManager*, clusterManagerFromProto_,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap));
  MOCK_METHOD(Http::ConnectionPool::Instance*, allocateConnPool_,
              (HostConstSharedPtr host, Network::ConnectionSocket::OptionsSharedPtr,
               Network::TransportSocketOptionsSharedPtr));
  MOCK_METHOD(Tcp::ConnectionPool::Instance*, allocateTcpConnPool_, (HostConstSharedPtr host));
  MOCK_METHOD((std::pair<ClusterSharedPtr, ThreadAwareLoadBalancer*>), clusterFromProto_,
              (const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
               Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api));
  MOCK_METHOD(CdsApi*, createCds_, ());

  Stats::TestUtil::TestStore stats_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager_{
      dispatcher_.timeSource()};
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  NiceMock<Secret::MockSecretManager> secret_manager_;
  NiceMock<AccessLog::MockAccessLogManager> log_manager_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_;
};

// Helper to intercept calls to postThreadLocalClusterUpdate.
class MockLocalClusterUpdate {
public:
  MOCK_METHOD(void, post,
              (uint32_t priority, const HostVector& hosts_added, const HostVector& hosts_removed));
};

class MockLocalHostsRemoved {
public:
  MOCK_METHOD(void, post, (const HostVector&));
};

// A test version of ClusterManagerImpl that provides a way to get a non-const handle to the
// clusters, which is necessary in order to call updateHosts on the priority set.
class TestClusterManagerImpl : public ClusterManagerImpl {
public:
  using ClusterManagerImpl::ClusterManagerImpl;

  TestClusterManagerImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                         ClusterManagerFactory& factory, Stats::Store& stats,
                         ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                         const LocalInfo::LocalInfo& local_info,
                         AccessLog::AccessLogManager& log_manager,
                         Event::Dispatcher& main_thread_dispatcher, Server::Admin& admin,
                         ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
                         Http::Context& http_context, Grpc::Context& grpc_context)
      : ClusterManagerImpl(bootstrap, factory, stats, tls, runtime, local_info, log_manager,
                           main_thread_dispatcher, admin, validation_context, api, http_context,
                           grpc_context) {}

  std::map<std::string, std::reference_wrapper<Cluster>> activeClusters() {
    std::map<std::string, std::reference_wrapper<Cluster>> clusters;
    for (auto& cluster : active_clusters_) {
      clusters.emplace(cluster.first, *cluster.second->cluster_);
    }
    return clusters;
  }
};

// Override postThreadLocalClusterUpdate so we can test that merged updates calls
// it with the right values at the right times.
class MockedUpdatedClusterManagerImpl : public TestClusterManagerImpl {
public:
  MockedUpdatedClusterManagerImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                  ClusterManagerFactory& factory, Stats::Store& stats,
                                  ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                                  const LocalInfo::LocalInfo& local_info,
                                  AccessLog::AccessLogManager& log_manager,
                                  Event::Dispatcher& main_thread_dispatcher, Server::Admin& admin,
                                  ProtobufMessage::ValidationContext& validation_context,
                                  Api::Api& api, MockLocalClusterUpdate& local_cluster_update,
                                  MockLocalHostsRemoved& local_hosts_removed,
                                  Http::Context& http_context, Grpc::Context& grpc_context)
      : TestClusterManagerImpl(bootstrap, factory, stats, tls, runtime, local_info, log_manager,
                               main_thread_dispatcher, admin, validation_context, api, http_context,
                               grpc_context),
        local_cluster_update_(local_cluster_update), local_hosts_removed_(local_hosts_removed) {}

protected:
  void postThreadLocalClusterUpdate(const Cluster&, uint32_t priority,
                                    const HostVector& hosts_added,
                                    const HostVector& hosts_removed) override {
    local_cluster_update_.post(priority, hosts_added, hosts_removed);
  }

  void postThreadLocalDrainConnections(const Cluster&, const HostVector& hosts_removed) override {
    local_hosts_removed_.post(hosts_removed);
  }

  MockLocalClusterUpdate& local_cluster_update_;
  MockLocalHostsRemoved& local_hosts_removed_;
};

} // namespace Upstream
} // namespace Envoy

#include <chrono>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/http/codec.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/network/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/cluster_factory_impl.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/proto/cluster_factory_config.pb.h"
#include "test/proto/cluster_factory_config.pb.validate.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

using testing::_;
using testing::ContainerEq;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {
namespace {

class TestStaticClusterImpl : public ClusterImplBase {
public:
  TestStaticClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                        Server::Configuration::TransportSocketFactoryContext& factory_context,
                        Stats::ScopePtr&& stats_scope, bool added_via_api, uint32_t priority)
      : ClusterImplBase(cluster, runtime, factory_context, std::move(stats_scope), added_via_api),
        priority_(priority) {}

  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  // ClusterImplBase
  void startPreInit() override {
    HostVectorSharedPtr hosts(new HostVector(
        {makeTestHost(this->info(), "tcp://127.0.0.1:80")}));
    HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
    HostVector hosts_added{hosts->front()};
    HostVector hosts_removed{};

    this->prioritySet().updateHosts(
        priority_,
        HostSetImpl::updateHostsParams(hosts, hosts_per_locality, hosts, hosts_per_locality), {},
        hosts_added, hosts_removed, absl::nullopt);
  }
  const uint32_t priority_;
};

class TestStaticClusterFactory : public ClusterFactoryImplBase {
public:
  TestStaticClusterFactory() : ClusterFactoryImplBase("envoy.clusters.test_static") {}

  ClusterImplBaseSharedPtr
  createClusterImpl(const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
                    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
                    Stats::ScopePtr&& stats_scope) override {
    return std::make_unique<TestStaticClusterImpl>(cluster, context.runtime(),
                                                   socket_factory_context, std::move(stats_scope),
                                                   context.addedViaApi(), 1);
  }
};

class ConfigurableTestStaticClusterFactory
    : public ConfigurableClusterFactoryBase<test::common::upstream::CustomStaticConfig> {
public:
  ConfigurableTestStaticClusterFactory()
      : ConfigurableClusterFactoryBase("envoy.clusters.test_static_config") {}

private:
  ClusterImplBaseSharedPtr createClusterWithConfig(
      const envoy::api::v2::Cluster& cluster,
      const test::common::upstream::CustomStaticConfig& proto_config,
      ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override {
    return std::make_unique<TestStaticClusterImpl>(cluster, context.runtime(),
                                                   socket_factory_context, std::move(stats_scope),
                                                   context.addedViaApi(), proto_config.priority());
  }
};

class ClusterFactoryTestBase {
protected:
  ClusterFactoryTestBase() : api_(Api::createApiForTest(stats_)) {
    outlier_event_logger_.reset(new Outlier::MockEventLogger());
    dns_resolver_.reset(new Network::MockDnsResolver());
  }

  NiceMock<Server::MockAdmin> admin_;
  Ssl::MockContextManager ssl_context_manager_;
  NiceMock<MockClusterManager> cm_;
  const NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest().currentThreadId()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  Api::ApiPtr api_;
  Network::DnsResolverSharedPtr dns_resolver_;
  AccessLog::MockAccessLogManager log_manager_;
  Outlier::EventLoggerSharedPtr outlier_event_logger_;
};

class TestStaticClusterImplTest : public testing::Test, public ClusterFactoryTestBase {};

TEST_F(TestStaticClusterImplTest, createWithoutConfig) {
  const std::string yaml = R"EOF(
      name: staticcluster
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      hosts:
      - socket_address:
          address: 10.0.0.1
          port_value: 443
      cluster_type:
        name: envoy.clusters.test_static
    )EOF";

  TestStaticClusterFactory factory;
  Registry::InjectFactory<ClusterFactory> registered_factory(factory);

  const envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  auto cluster = ClusterFactoryImplBase::create(
      cluster_config, cm_, stats_, tls_, dns_resolver_, ssl_context_manager_, runtime_, random_,
      dispatcher_, log_manager_, local_info_, admin_, singleton_manager_,
      std::move(outlier_event_logger_), false, *api_);
  cluster->initialize([] {});

  EXPECT_EQ(1UL, cluster->prioritySet().hostSetsPerPriority()[1]->healthyHosts().size());
  EXPECT_EQ("", cluster->prioritySet().hostSetsPerPriority()[1]->hosts()[0]->hostname());
  EXPECT_EQ("127.0.0.1", cluster->prioritySet()
                             .hostSetsPerPriority()[1]
                             ->hosts()[0]
                             ->address()
                             ->ip()
                             ->addressAsString());
  EXPECT_EQ(80,
            cluster->prioritySet().hostSetsPerPriority()[1]->hosts()[0]->address()->ip()->port());
  EXPECT_FALSE(cluster->info()->addedViaApi());
}

TEST_F(TestStaticClusterImplTest, createWithStructConfig) {
  const std::string yaml = R"EOF(
      name: staticcluster
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      hosts:
      - socket_address:
          address: 10.0.0.1
          port_value: 443
      cluster_type:
          name: envoy.clusters.test_static_config
          typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
            value:
              priority: 10
    )EOF";

  ConfigurableTestStaticClusterFactory factory;
  Registry::InjectFactory<ClusterFactory> registered_factory(factory);

  const envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  auto cluster = ClusterFactoryImplBase::create(
      cluster_config, cm_, stats_, tls_, dns_resolver_, ssl_context_manager_, runtime_, random_,
      dispatcher_, log_manager_, local_info_, admin_, singleton_manager_,
      std::move(outlier_event_logger_), false, *api_);
  cluster->initialize([] {});

  EXPECT_EQ(1UL, cluster->prioritySet().hostSetsPerPriority()[10]->healthyHosts().size());
  EXPECT_EQ("", cluster->prioritySet().hostSetsPerPriority()[10]->hosts()[0]->hostname());
  EXPECT_EQ("127.0.0.1", cluster->prioritySet()
                             .hostSetsPerPriority()[10]
                             ->hosts()[0]
                             ->address()
                             ->ip()
                             ->addressAsString());
  EXPECT_EQ(80,
            cluster->prioritySet().hostSetsPerPriority()[10]->hosts()[0]->address()->ip()->port());
  EXPECT_FALSE(cluster->info()->addedViaApi());
}

TEST_F(TestStaticClusterImplTest, createWithTypedConfig) {
  const std::string yaml = R"EOF(
      name: staticcluster
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      hosts:
      - socket_address:
          address: 10.0.0.1
          port_value: 443
      cluster_type:
          name: envoy.clusters.test_static_config
          typed_config:
            "@type": type.googleapis.com/test.common.upstream.CustomStaticConfig
            priority: 10
    )EOF";

  ConfigurableTestStaticClusterFactory factory;
  Registry::InjectFactory<ClusterFactory> registered_factory(factory);

  const envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  auto cluster = ClusterFactoryImplBase::create(
      cluster_config, cm_, stats_, tls_, dns_resolver_, ssl_context_manager_, runtime_, random_,
      dispatcher_, log_manager_, local_info_, admin_, singleton_manager_,
      std::move(outlier_event_logger_), false, *api_);
  cluster->initialize([] {});

  EXPECT_EQ(1UL, cluster->prioritySet().hostSetsPerPriority()[10]->healthyHosts().size());
  EXPECT_EQ("", cluster->prioritySet().hostSetsPerPriority()[10]->hosts()[0]->hostname());
  EXPECT_EQ("127.0.0.1", cluster->prioritySet()
                             .hostSetsPerPriority()[10]
                             ->hosts()[0]
                             ->address()
                             ->ip()
                             ->addressAsString());
  EXPECT_EQ(80,
            cluster->prioritySet().hostSetsPerPriority()[10]->hosts()[0]->address()->ip()->port());
  EXPECT_FALSE(cluster->info()->addedViaApi());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
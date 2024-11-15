#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/http/codec.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/network/utility.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/integration/clusters/cluster_factory_config.pb.validate.h"
#include "test/integration/clusters/custom_static_cluster.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/options.h"
#include "test/mocks/ssl/mocks.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {
namespace {

// Test Cluster Factory without custom configuration
class TestStaticClusterFactory : public Event::TestUsingSimulatedTime,
                                 public ClusterFactoryImplBase {

public:
  TestStaticClusterFactory() : ClusterFactoryImplBase("envoy.clusters.test_static") {}

  absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
  createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context) override {
    absl::Status creation_status = absl::OkStatus();
    return std::make_pair(std::make_shared<CustomStaticCluster>(cluster, context, 1, "127.0.0.1",
                                                                80, creation_status),
                          nullptr);
  }
};

class ClusterFactoryTestBase {
protected:
  ClusterFactoryTestBase() {
    outlier_event_logger_ = std::make_shared<Outlier::MockEventLogger>();
    dns_resolver_ = std::make_shared<Network::MockDnsResolver>();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Ssl::MockContextManager ssl_context_manager_;
  NiceMock<MockClusterManager> cm_;
  Network::DnsResolverSharedPtr dns_resolver_;
  std::function<Network::DnsResolverSharedPtr()> dns_resolver_fn_ =
      [this]() -> Network::DnsResolverSharedPtr { return this->dns_resolver_; };
  Outlier::EventLoggerSharedPtr outlier_event_logger_;
};

class TestStaticClusterImplTest : public testing::Test, public ClusterFactoryTestBase {};

TEST_F(TestStaticClusterImplTest, CreateWithoutConfig) {
  const std::string yaml = R"EOF(
      name: staticcluster
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 443
      cluster_type:
        name: envoy.clusters.test_static
    )EOF";

  TestStaticClusterFactory factory;
  Registry::InjectFactory<ClusterFactory> registered_factory(factory);

  const envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  auto create_result =
      ClusterFactoryImplBase::create(cluster_config, server_context_, cm_, dns_resolver_fn_,
                                     ssl_context_manager_, std::move(outlier_event_logger_), false);
  auto cluster = create_result->first;
  cluster->initialize([] {});

  EXPECT_EQ(1UL, cluster->prioritySet().hostSetsPerPriority()[1]->healthyHosts().size());
  EXPECT_EQ("", cluster->prioritySet().hostSetsPerPriority()[1]->hosts()[0]->hostname());
  // the hosts field override by values hardcoded in the factory
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

TEST_F(TestStaticClusterImplTest, CreateWithStructConfig) {
  const std::string yaml = R"EOF(
      name: staticcluster
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 443
      cluster_type:
          name: envoy.clusters.custom_static
          typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
            value:
              priority: 10
              address: 127.0.0.1
              port_value: 80
    )EOF";

  const envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  auto create_result =
      ClusterFactoryImplBase::create(cluster_config, server_context_, cm_, dns_resolver_fn_,
                                     ssl_context_manager_, std::move(outlier_event_logger_), false);
  auto cluster = create_result->first;
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

TEST_F(TestStaticClusterImplTest, CreateWithTypedConfig) {
  const std::string yaml = R"EOF(
      name: staticcluster
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 443
      cluster_type:
          name: envoy.clusters.custom_static
          typed_config:
            "@type": type.googleapis.com/test.integration.clusters.CustomStaticConfig
            priority: 10
            address: 127.0.0.1
            port_value: 80
    )EOF";

  const envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  auto create_result =
      ClusterFactoryImplBase::create(cluster_config, server_context_, cm_, dns_resolver_fn_,
                                     ssl_context_manager_, std::move(outlier_event_logger_), false);
  auto cluster = create_result->first;
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

TEST_F(TestStaticClusterImplTest, UnsupportedClusterType) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 443
    cluster_type:
        name: envoy.clusters.bad_cluster_name
        typed_config:
          "@type": type.googleapis.com/test.integration.clusters.CustomStaticConfig
          priority: 10
  )EOF";
  // the factory is not registered, expect to fail
  const envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  auto create_result =
      ClusterFactoryImplBase::create(cluster_config, server_context_, cm_, dns_resolver_fn_,
                                     ssl_context_manager_, std::move(outlier_event_logger_), false);
  EXPECT_FALSE(create_result.ok());
  EXPECT_EQ(create_result.status().message(),
            "Didn't find a registered cluster factory implementation for name: "
            "'envoy.clusters.bad_cluster_name'");
}

TEST_F(TestStaticClusterImplTest, HostnameWithoutDNS) {
  const std::string yaml = R"EOF(
      name: staticcluster
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      common_lb_config:
        consistent_hashing_lb_config:
          use_hostname_for_hashing: true
      load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 443
      cluster_type:
        name: envoy.clusters.test_static
    )EOF";

  const envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  auto create_result =
      ClusterFactoryImplBase::create(cluster_config, server_context_, cm_, dns_resolver_fn_,
                                     ssl_context_manager_, std::move(outlier_event_logger_), false);
  EXPECT_FALSE(create_result.ok());
  EXPECT_EQ(create_result.status().message(),
            "Cannot use hostname for consistent hashing loadbalancing for cluster of type: "
            "'envoy.clusters.test_static'");
}

} // namespace
} // namespace Upstream
} // namespace Envoy

#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"

#include "test/mocks/stream_info/mocks.h"

#include "absl/strings/str_cat.h"
#include "test/extensions/clusters/reverse_connection/reverse_connection_cluster_test_base.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

TEST_F(ReverseConnectionClusterTest, HeaderFormatterExpressions) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-node-id)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("production-node", "cluster-production");

  RevConCluster::LoadBalancer lb(cluster_);

  // Test header extraction.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-node-id", "production-node"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "production-node");
  }
}

// Test multiple header formatter combinations.
TEST_F(ReverseConnectionClusterTest, MultipleHeaderFormatters) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-env)%-%REQ(x-node-id)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("prod-node-123", "cluster-prod");
  addTestSocket("dev-node-456", "cluster-dev");

  RevConCluster::LoadBalancer lb(cluster_);

  // Test 1: Production environment with node-123.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-env", "prod"}, {"x-node-id", "node-123"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "prod-node-123");
  }

  // Test 2: Development environment with node-456.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-env", "dev"}, {"x-node-id", "node-456"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "dev-node-456");
  }

  // Test 3: Missing header results in host creation (formatter returns "prod--" when x-node-id is
  // missing).
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-env", "prod"}}}; // Missing x-node-id

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr); // Host created for "prod--" (missing node-id becomes "-")
    EXPECT_EQ(result.host->address()->logicalName(), "prod--");
  }
}

// Test connection property formatters.
TEST_F(ReverseConnectionClusterTest, ConnectionPropertyFormatters) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "conn-%DOWNSTREAM_REMOTE_ADDRESS%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("conn-192.168.1.100:8080", "cluster-conn");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create stream info with connection info.
  auto stream_info = std::make_unique<NiceMock<StreamInfo::MockStreamInfo>>();
  auto remote = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.100", 8080);
  auto local = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0", 0);
  Network::ConnectionInfoSetterImpl conn_info(local, remote);
  ON_CALL(*stream_info, downstreamAddressProvider()).WillByDefault(ReturnRef(conn_info));

  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, streamInfo()).WillByDefault(testing::ReturnRef(*stream_info));
  TestLoadBalancerContext lb_context(&connection, stream_info.get());
  lb_context.downstream_headers_ =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-header", "value"}}};

  auto result = lb.chooseHost(&lb_context);
  ASSERT_NE(result.host, nullptr);
  EXPECT_EQ(result.host->address()->logicalName(), "conn-192.168.1.100:8080");
}

// Test formatter error handling and edge cases.
TEST_F(ReverseConnectionClusterTest, FormatterErrorHandling) {
  // Test missing header returns nullptr.
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-missing-header)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();

  RevConCluster::LoadBalancer lb(cluster_);
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  lb_context.downstream_headers_ =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-other-header", "value"}}};

  // Should return nullptr for missing header.
  auto result = lb.chooseHost(&lb_context);
  EXPECT_EQ(result.host, nullptr);
}

// Test formatter with complex combinations and transformations.
TEST_F(ReverseConnectionClusterTest, ComplexFormatterCombinations) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "svc-%REQ(x-service)%-env-%REQ(x-env)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("svc-api-env-prod", "cluster-complex");

  RevConCluster::LoadBalancer lb(cluster_);

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
      new Http::TestRequestHeaderMapImpl{{"x-service", "api"}, {"x-env", "prod"}}};

  auto result = lb.chooseHost(&lb_context);
  ASSERT_NE(result.host, nullptr);
  EXPECT_EQ(result.host->address()->logicalName(), "svc-api-env-prod");
}

// Test scalability with many different host IDs.
TEST_F(ReverseConnectionClusterTest, ScalabilityManyHostIds) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "node-%REQ(x-node-index)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();

  RevConCluster::LoadBalancer lb(cluster_);

  // Create sockets for 100 different nodes.
  for (int i = 0; i < 100; ++i) {
    std::string node_id = absl::StrCat("node-", i);
    addTestSocket(node_id, absl::StrCat("cluster-", i));
  }

  // Verify each node gets its own host.
  for (int i = 0; i < 100; ++i) {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    std::string node_index = absl::StrCat(i);
    lb_context.downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-node-index", node_index}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), absl::StrCat("node-", i));
  }
}

TEST_F(ReverseConnectionClusterTest, ConcurrentHostCreation) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-concurrent-node)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("concurrent-node-1", "cluster-concurrent");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create multiple concurrent requests for the same node.
  std::vector<Upstream::HostConstSharedPtr> hosts;
  for (int i = 0; i < 10; ++i) {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-concurrent-node", "concurrent-node-1"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    hosts.push_back(result.host);
  }

  // All should return the same cached host.
  for (size_t i = 1; i < hosts.size(); ++i) {
    EXPECT_EQ(hosts[0], hosts[i]);
  }
}

// Test comprehensive formatter functionality with various format strings.
TEST_F(ReverseConnectionClusterTest, FormatterComprehensiveTests) {
  // Test 1: Simple header extraction.
  {
    const std::string yaml = R"EOF(
      name: name
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cleanup_interval: 1s
      cluster_type:
        name: envoy.clusters.reverse_connection
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
          cleanup_interval: 10s
          host_id_format: "%REQ(x-node-id)%"
    )EOF";

    setupFromYaml(yaml);
    setupUpstreamExtension();
    setupThreadLocalSlot();
    addTestSocket("node-123", "cluster-123");

    RevConCluster::LoadBalancer lb(cluster_);
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-node-id", "node-123"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "node-123");
  }

  // Test 2: Combined format with multiple headers.
  {
    const std::string yaml = R"EOF(
      name: name
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cleanup_interval: 1s
      cluster_type:
        name: envoy.clusters.reverse_connection
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
          cleanup_interval: 10s
          host_id_format: "%REQ(x-tenant)%-%REQ(x-region)%"
    )EOF";

    setupFromYaml(yaml);
    setupUpstreamExtension();
    setupThreadLocalSlot();
    addTestSocket("tenant-a-us-west", "cluster-multi");

    RevConCluster::LoadBalancer lb(cluster_);
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-tenant", "tenant-a"}, {"x-region", "us-west"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "tenant-a-us-west");
  }

  // Test 3: Missing header should result in no host.
  {
    const std::string yaml = R"EOF(
      name: name
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cleanup_interval: 1s
      cluster_type:
        name: envoy.clusters.reverse_connection
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
          cleanup_interval: 10s
          host_id_format: "%REQ(x-missing-header)%"
    )EOF";

    setupFromYaml(yaml);
    setupUpstreamExtension();
    setupThreadLocalSlot();

    RevConCluster::LoadBalancer lb(cluster_);
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-other-header", "value"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_EQ(result.host, nullptr);
  }
}

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy

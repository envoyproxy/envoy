#include "test/extensions/clusters/reverse_connection/reverse_connection_cluster_test_base.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

TEST_F(ReverseConnectionClusterTest, BasicSetup) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

// Test host creation failure due to no context.
TEST_F(ReverseConnectionClusterTest, NoContext) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // No downstream connection => no host.
  {
    TestLoadBalancerContext lb_context(nullptr);
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }

  // Test null context. It should return a nullptr.
  {
    RevConCluster::LoadBalancer lb(cluster_);
    Upstream::HostConstSharedPtr host = lb.chooseHost(nullptr).host;
    EXPECT_EQ(host, nullptr);
  }
}

// Test host creation failure due to no headers.
TEST_F(ReverseConnectionClusterTest, NoHeaders) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Downstream connection but no headers => no host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }
}

// Test host creation failure due to missing required headers.
TEST_F(ReverseConnectionClusterTest, MissingRequiredHeaders) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Request with unsupported headers but missing all required headers (EnvoyDstNodeUUID,.
  // EnvoyDstClusterUUID, proper Host header).
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-random-header", "random-value");
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }

  // Test with empty header value. This should be skipped and continue to next header.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-remote-node-id", "");
    RevConCluster::LoadBalancer lb(cluster_);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }
}

TEST_F(ReverseConnectionClusterTest, HostCreationWithSocketManager) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  // Set up the thread local slot, initializing the socket manager.
  setupThreadLocalSlot();

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Test host creation with Host header.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "test-uuid-123");
  }

  // Test host creation with header mapping to a different node id (test-uuid-456).
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-456"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "test-uuid-456");
  }

  // Test host creation with HTTP headers.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-remote-node-id", "test-uuid-123");

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "test-uuid-123");
  }
}

TEST_F(ReverseConnectionClusterTest, HostIdExtractionWithSafeRegex) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Initialize upstream extension and thread local registry for socket manager.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Provide reverse connection sockets for different host_ids.
  addTestSocket("foo.bar", "cluster-foo-bar");
  addTestSocket("node-123", "cluster-node-123");

  RevConCluster::LoadBalancer lb(cluster_);

  // Request: header value "foo.bar" routes to host_id "foo.bar".
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "foo.bar"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "foo.bar");
  }

  // Request: header value "node-123" routes to host_id "node-123".
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "node-123"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "node-123");
  }

  // Request: header value "unknown-node" creates a host even without socket.
  // The socket availability check happens at connection time, not host selection time.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "unknown-node"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "unknown-node");
  }
}

// Test host reuse for requests with same UUID.
TEST_F(ReverseConnectionClusterTest, HostReuse) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Add test socket to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);

    // Create second host with same UUID. We should reuse the same host.
    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    EXPECT_EQ(result1.host, result2.host);
  }
}

// Test different hosts for different UUIDs.
TEST_F(ReverseConnectionClusterTest, DifferentHostsForDifferentUUIDs) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);

    // Create second host with different UUID. We should use a different host.
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-456"}}};
    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    EXPECT_NE(result1.host, result2.host);
  }
}

// Test cleanup of hosts.
TEST_F(ReverseConnectionClusterTest, TestCleanup) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create two hosts.
  Upstream::HostSharedPtr host1, host2;

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);
    host1 = std::const_pointer_cast<Upstream::Host>(result1.host);
  }

  // Create second host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-456"}}};

    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    host2 = std::const_pointer_cast<Upstream::Host>(result2.host);
  }

  // Verify hosts are different.
  EXPECT_NE(host1, host2);

  // Expect the cleanup timer to be enabled after cleanup.
  EXPECT_CALL(*cleanup_timer_, enableTimer(std::chrono::milliseconds(10000), nullptr));

  // Call cleanup via the helper method.
  callCleanup();

  // Verify that hosts can still be accessed after cleanup.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
  }
}

// Test cleanup of hosts with used hosts.
TEST_F(ReverseConnectionClusterTest, TestCleanupWithUsedHosts) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create two hosts.
  Upstream::HostSharedPtr host1, host2;

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);
    host1 = std::const_pointer_cast<Upstream::Host>(result1.host);
  }

  // Create second host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-456"}}};

    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    host2 = std::const_pointer_cast<Upstream::Host>(result2.host);
  }

  // Mark one host as used by acquiring a handle.
  auto handle1 = host1->acquireHandle();
  EXPECT_TRUE(host1->used());

  // Expect the cleanup timer to be enabled after cleanup.
  EXPECT_CALL(*cleanup_timer_, enableTimer(std::chrono::milliseconds(10000), nullptr));

  // Call cleanup via the helper method.
  callCleanup();

  // Verify that the used host is still accessible after cleanup.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
  }

  // Release the handle.
  handle1.reset();
}

// LoadBalancerFactory tests.
TEST_F(ReverseConnectionClusterTest, LoadBalancerFactory) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Test LoadBalancerFactory using helper method.
  auto factory = createLoadBalancerFactory();
  EXPECT_NE(factory, nullptr);

  // Test that the factory creates load balancers.
  Upstream::LoadBalancerParams params{cluster_->prioritySet()};
  auto lb = factory->create(params);
  EXPECT_NE(lb, nullptr);

  // Test that multiple load balancers are different instances.
  auto lb2 = factory->create(params);
  EXPECT_NE(lb2, nullptr);
  EXPECT_NE(lb.get(), lb2.get());

  // Test create() without parameters.
  auto lb3 = factory->create();
  EXPECT_NE(lb3, nullptr);
}

// ThreadAwareLoadBalancer tests
TEST_F(ReverseConnectionClusterTest, ThreadAwareLoadBalancer) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Test ThreadAwareLoadBalancer using helper method.
  auto thread_aware_lb = createThreadAwareLoadBalancer();
  EXPECT_NE(thread_aware_lb, nullptr);

  // Test initialize() method.
  auto init_status = thread_aware_lb->initialize();
  EXPECT_TRUE(init_status.ok());

  // Test factory() method.
  auto factory = thread_aware_lb->factory();
  EXPECT_NE(factory, nullptr);

  // Test that factory creates load balancers.
  Upstream::LoadBalancerParams params{cluster_->prioritySet()};
  auto lb = factory->create(params);
  EXPECT_NE(lb, nullptr);
}

// Test no-op methods for load balancer.
TEST_F(ReverseConnectionClusterTest, LoadBalancerNoopMethods) {
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
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  RevConCluster::LoadBalancer lb(cluster_);

  // Test peekAnotherHost. It should return a nullptr.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    Upstream::HostConstSharedPtr peeked_host = lb.peekAnotherHost(&lb_context);
    EXPECT_EQ(peeked_host, nullptr);
  }

  // Test selectExistingConnection. It should return a nullopt.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    std::vector<uint8_t> hash_key;

    // Create a mock host for testing.
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
    auto selected_connection = lb.selectExistingConnection(&lb_context, *mock_host, hash_key);
    EXPECT_FALSE(selected_connection.has_value());
  }

  // Test lifetimeCallbacks. It should return an empty OptRef.
  {
    auto lifetime_callbacks = lb.lifetimeCallbacks();
    EXPECT_FALSE(lifetime_callbacks.has_value());
  }
}

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy

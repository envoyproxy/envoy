#include "test/extensions/clusters/reverse_connection/reverse_connection_cluster_test_base.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

TEST(ReverseConnectionClusterConfigTest, ValidConfig) {
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

  envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
  EXPECT_TRUE(cluster_config.has_cluster_type());
  EXPECT_EQ(cluster_config.cluster_type().name(), "envoy.clusters.reverse_connection");
}

// Test cluster creation failure due to invalid load assignment.

TEST_F(ReverseConnectionClusterTest, BadConfigWithLoadAssignment) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    load_assignment:
      cluster_name: name
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8000
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                            "Reverse Conn clusters must have no load assignment configured");
}

// Test cluster creation failure due to wrong load balancing policy.

TEST_F(ReverseConnectionClusterTest, BadConfigWithWrongLbPolicy) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: ROUND_ROBIN
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                            "cluster: LB policy ROUND_ROBIN is not valid for Cluster type "
                            "envoy.clusters.reverse_connection. Only 'CLUSTER_PROVIDED' is allowed "
                            "with cluster type 'REVERSE_CONNECTION'");
}


TEST_F(ReverseConnectionClusterTest, RequiresBootstrapExtension) {
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

  // Verify cluster was created successfully since bootstrap extension exists.
  EXPECT_NE(cluster_, nullptr);
}

// Test when the socket interface is not registered. In this case, cluster creation should fail.

TEST_F(ReverseConnectionClusterTest, SocketInterfaceNotRegistered) {
  // Temporarily remove the upstream reverse connection socket interface from the registry.
  // This will make Network::socketInterface() return nullptr for the specific name.
  auto saved_factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();

  // Find and remove the specific socket interface factory.
  auto& factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();
  auto it = factories.find("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  if (it != factories.end()) {
    factories.erase(it);
  }

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

  // Cluster creation should fail with a clear error message.
  EXPECT_THROW_WITH_MESSAGE(
      setupFromYaml(yaml, false), EnvoyException,
      "Reverse connection cluster requires the upstream reverse tunnel bootstrap extension "
      "'envoy.bootstrap.reverse_tunnel.upstream_socket_interface' to be configured. Please add it "
      "to bootstrap_extensions in your bootstrap configuration.");

  // Restore the registry.
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories() =
      saved_factories;
}

// Test when the socket interface is registered but is the wrong type.

TEST_F(ReverseConnectionClusterTest, SocketInterfaceWrongType) {
  // Create a mock bootstrap extension factory that is NOT a ReverseTunnelAcceptor.
  class WrongTypeFactory : public Server::Configuration::BootstrapExtensionFactory {
  public:
    std::string name() const override {
      return "envoy.bootstrap.reverse_tunnel.upstream_socket_interface";
    }

    Server::BootstrapExtensionPtr
    createBootstrapExtension(const Protobuf::Message&,
                             Server::Configuration::ServerFactoryContext&) override {
      return nullptr;
    }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }
  };

  // Save current factories.
  auto saved_factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();

  // Register the wrong type factory.
  WrongTypeFactory wrong_factory;
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories()
      ["envoy.bootstrap.reverse_tunnel.upstream_socket_interface"] = &wrong_factory;

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

  // Cluster creation should fail because the factory is not a ReverseTunnelAcceptor.
  EXPECT_THROW_WITH_MESSAGE(
      setupFromYaml(yaml, false), EnvoyException,
      "Bootstrap extension 'envoy.bootstrap.reverse_tunnel.upstream_socket_interface' exists but "
      "is not of the expected type (ReverseTunnelAcceptor). This indicates a configuration error.");

  // Restore the registry.
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories() =
      saved_factories;
}

TEST_F(ReverseConnectionClusterTest, FormatterValidationErrors) {
  // Test invalid format string.
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
          host_id_format: "%INVALID_COMMAND()%"
    )EOF";

    EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                              "Not supported field in StreamInfo: INVALID_COMMAND");
  }
}

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy

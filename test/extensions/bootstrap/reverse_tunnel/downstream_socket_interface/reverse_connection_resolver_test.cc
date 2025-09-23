#include "envoy/config/core/v3/address.pb.h"

#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_resolver.h"

#include "test/test_common/logging.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionResolverTest : public testing::Test {
protected:
  void SetUp() override {}

  // Helper function to create a valid socket address.
  envoy::config::core::v3::SocketAddress createSocketAddress(const std::string& address,
                                                             uint32_t port = 0) {
    envoy::config::core::v3::SocketAddress socket_address;
    socket_address.set_address(address);
    socket_address.set_port_value(port);
    return socket_address;
  }

  // Helper function to create a valid reverse connection address string.
  std::string createReverseConnectionAddress(const std::string& src_node_id,
                                             const std::string& src_cluster_id,
                                             const std::string& src_tenant_id,
                                             const std::string& cluster_name, uint32_t count) {
    return fmt::format("rc://{}:{}:{}@{}:{}", src_node_id, src_cluster_id, src_tenant_id,
                       cluster_name, count);
  }

  // Helper function to access the private extractReverseConnectionConfig method.
  absl::StatusOr<ReverseConnectionAddress::ReverseConnectionConfig>
  extractReverseConnectionConfig(const envoy::config::core::v3::SocketAddress& socket_address) {
    return resolver_.extractReverseConnectionConfig(socket_address);
  }

  ReverseConnectionResolver resolver_;
  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

// Test the name() method.
TEST_F(ReverseConnectionResolverTest, Name) {
  EXPECT_EQ(resolver_.name(), "envoy.resolvers.reverse_connection");
}

// Test successful resolution of a valid reverse connection address.
TEST_F(ReverseConnectionResolverTest, ResolveValidAddress) {
  std::string address_str = createReverseConnectionAddress("test-node", "test-cluster",
                                                           "test-tenant", "remote-cluster", 5);
  auto socket_address = createSocketAddress(address_str);

  auto result = resolver_.resolve(socket_address);
  EXPECT_TRUE(result.ok());

  auto resolved_address = result.value();
  EXPECT_NE(resolved_address, nullptr);

  // Verify it's a ReverseConnectionAddress.
  auto reverse_address =
      std::dynamic_pointer_cast<const ReverseConnectionAddress>(resolved_address);
  EXPECT_NE(reverse_address, nullptr);

  // Verify the configuration.
  const auto& config = reverse_address->reverseConnectionConfig();
  EXPECT_EQ(config.src_node_id, "test-node");
  EXPECT_EQ(config.src_cluster_id, "test-cluster");
  EXPECT_EQ(config.src_tenant_id, "test-tenant");
  EXPECT_EQ(config.remote_cluster, "remote-cluster");
  EXPECT_EQ(config.connection_count, 5);
}

// Test resolution failure for non-reverse connection address.
TEST_F(ReverseConnectionResolverTest, ResolveNonReverseConnectionAddress) {
  auto socket_address = createSocketAddress("127.0.0.1");

  auto result = resolver_.resolve(socket_address);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Address must start with 'rc://'"));
}

// Test resolution failure for non-zero port.
TEST_F(ReverseConnectionResolverTest, ResolveNonZeroPort) {
  std::string address_str = createReverseConnectionAddress("test-node", "test-cluster",
                                                           "test-tenant", "remote-cluster", 5);
  auto socket_address = createSocketAddress(address_str, 8080); // Non-zero port

  auto result = resolver_.resolve(socket_address);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Only port 0 is supported"));
}

// Test successful extraction of reverse connection config.
TEST_F(ReverseConnectionResolverTest, ExtractReverseConnectionConfigValid) {
  std::string address_str = createReverseConnectionAddress("node-123", "cluster-456", "tenant-789",
                                                           "remote-cluster-abc", 10);
  auto socket_address = createSocketAddress(address_str);

  auto result = extractReverseConnectionConfig(socket_address);
  EXPECT_TRUE(result.ok());

  const auto& config = result.value();
  EXPECT_EQ(config.src_node_id, "node-123");
  EXPECT_EQ(config.src_cluster_id, "cluster-456");
  EXPECT_EQ(config.src_tenant_id, "tenant-789");
  EXPECT_EQ(config.remote_cluster, "remote-cluster-abc");
  EXPECT_EQ(config.connection_count, 10);
}

// Test resolution failure for invalid format,
TEST_F(ReverseConnectionResolverTest, ResolveInvalidFormat) {
  auto socket_address = createSocketAddress("rc://node:cluster:tenant:cluster:5"); // Missing @

  auto result = resolver_.resolve(socket_address);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Invalid reverse connection address format"));
}

// Test extraction failure for invalid source info format.
TEST_F(ReverseConnectionResolverTest, ExtractReverseConnectionConfigInvalidSourceInfo) {
  auto socket_address = createSocketAddress("rc://node:cluster@remote:5"); // Missing tenant_id

  auto result = extractReverseConnectionConfig(socket_address);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Invalid source info format"));
}

// Test extraction failure for empty node ID.
TEST_F(ReverseConnectionResolverTest, ExtractReverseConnectionConfigEmptyNodeId) {
  auto socket_address = createSocketAddress("rc://:cluster:tenant@remote:5");

  auto result = extractReverseConnectionConfig(socket_address);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Source node ID cannot be empty"));
}

// Test extraction failure for empty cluster ID.
TEST_F(ReverseConnectionResolverTest, ExtractReverseConnectionConfigEmptyClusterId) {
  auto socket_address = createSocketAddress("rc://node::tenant@remote:5");

  auto result = extractReverseConnectionConfig(socket_address);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Source cluster ID cannot be empty"));
}

// Test extraction failure for invalid cluster config format.
TEST_F(ReverseConnectionResolverTest, ExtractReverseConnectionConfigInvalidClusterConfig) {
  auto socket_address = createSocketAddress("rc://node:cluster:tenant@remote"); // Missing count

  auto result = extractReverseConnectionConfig(socket_address);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Invalid cluster config format"));
}

// Test extraction failure for invalid connection count.
TEST_F(ReverseConnectionResolverTest, ExtractReverseConnectionConfigInvalidCount) {
  auto socket_address = createSocketAddress("rc://node:cluster:tenant@remote:invalid");

  auto result = extractReverseConnectionConfig(socket_address);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Invalid connection count"));
}

// Test extraction with zero connection count.
TEST_F(ReverseConnectionResolverTest, ExtractReverseConnectionConfigZeroCount) {
  std::string address_str =
      createReverseConnectionAddress("node-123", "cluster-456", "tenant-789", "remote-cluster", 0);
  auto socket_address = createSocketAddress(address_str);

  auto result = extractReverseConnectionConfig(socket_address);
  EXPECT_TRUE(result.ok());

  const auto& config = result.value();
  EXPECT_EQ(config.connection_count, 0);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

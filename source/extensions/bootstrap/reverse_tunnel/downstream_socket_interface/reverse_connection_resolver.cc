#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_resolver.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

absl::StatusOr<Network::Address::InstanceConstSharedPtr>
ReverseConnectionResolver::resolve(const envoy::config::core::v3::SocketAddress& socket_address) {

  // Check if address starts with rc://
  // Expected format: "rc://src_node_id:src_cluster_id:src_tenant_id@cluster_name:count"
  const std::string& address_str = socket_address.address();
  if (!absl::StartsWith(address_str, "rc://")) {
    return absl::InvalidArgumentError(fmt::format(
        "Address must start with 'rc://' for reverse connection resolver. "
        "Expected format: rc://src_node_id:src_cluster_id:src_tenant_id@cluster_name:count"));
  }

  // For reverse connections, only port 0 is supported.
  if (socket_address.port_value() != 0) {
    return absl::InvalidArgumentError(
        fmt::format("Only port 0 is supported for reverse connections. Got port: {}",
                    socket_address.port_value()));
  }

  // Extract reverse connection config.
  auto reverse_conn_config_or_error = extractReverseConnectionConfig(socket_address);
  if (!reverse_conn_config_or_error.ok()) {
    return reverse_conn_config_or_error.status();
  }

  // Create and return ReverseConnectionAddress.
  auto reverse_conn_address =
      std::make_shared<ReverseConnectionAddress>(reverse_conn_config_or_error.value());

  return reverse_conn_address;
}

absl::StatusOr<ReverseConnectionAddress::ReverseConnectionConfig>
ReverseConnectionResolver::extractReverseConnectionConfig(
    const envoy::config::core::v3::SocketAddress& socket_address) {

  const std::string& address_str = socket_address.address();

  // Parse the reverse connection URL format.
  std::string config_part = address_str.substr(5); // Remove "rc://" prefix

  // Split by '@' to separate source info from cluster config.
  std::vector<std::string> parts = absl::StrSplit(config_part, '@');
  if (parts.size() != 2) {
    return absl::InvalidArgumentError(
        "Invalid reverse connection address format. Expected: "
        "rc://src_node_id:src_cluster_id:src_tenant_id@cluster_name:count");
  }

  // Parse source info (node_id:cluster_id:tenant_id)
  std::vector<std::string> source_parts = absl::StrSplit(parts[0], ':');
  if (source_parts.size() != 3) {
    return absl::InvalidArgumentError(
        "Invalid source info format. Expected: src_node_id:src_cluster_id:src_tenant_id");
  }

  // Validate that node_id and cluster_id are not empty.
  if (source_parts[0].empty()) {
    return absl::InvalidArgumentError("Source node ID cannot be empty");
  }
  if (source_parts[1].empty()) {
    return absl::InvalidArgumentError("Source cluster ID cannot be empty");
  }

  // Parse cluster configuration (cluster_name:count)
  std::vector<std::string> cluster_parts = absl::StrSplit(parts[1], ':');
  if (cluster_parts.size() != 2) {
    return absl::InvalidArgumentError(
        fmt::format("Invalid cluster config format: {}. Expected: cluster_name:count", parts[1]));
  }

  uint32_t count;
  if (!absl::SimpleAtoi(cluster_parts[1], &count)) {
    return absl::InvalidArgumentError(
        fmt::format("Invalid connection count: {}", cluster_parts[1]));
  }

  // Create the config struct.
  ReverseConnectionAddress::ReverseConnectionConfig config;
  config.src_node_id = source_parts[0];
  config.src_cluster_id = source_parts[1];
  config.src_tenant_id = source_parts[2];
  config.remote_cluster = cluster_parts[0];
  config.connection_count = count;

  ENVOY_LOG(
      debug,
      "reverse connection config: node_id={}, cluster_id={}, tenant_id={}, remote_cluster={}, "
      "count={}",
      config.src_node_id, config.src_cluster_id, config.src_tenant_id, config.remote_cluster,
      config.connection_count);

  return config;
}

// Register the factory.
REGISTER_FACTORY(ReverseConnectionResolver, Network::Address::Resolver);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

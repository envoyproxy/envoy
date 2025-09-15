#pragma once

#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_address.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Custom address resolver that can create ReverseConnectionAddress instances
 * when reverse connection metadata is detected in the socket address.
 */
class ReverseConnectionResolver : public Network::Address::Resolver,
                                  public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
public:
  ReverseConnectionResolver() = default;

  // Network::Address::Resolver
  absl::StatusOr<Network::Address::InstanceConstSharedPtr>
  resolve(const envoy::config::core::v3::SocketAddress& socket_address) override;

  std::string name() const override { return "envoy.resolvers.reverse_connection"; }

  // Friend class for testing
  friend class ReverseConnectionResolverTest;

private:
  /**
   * Extracts reverse connection config from socket address metadata.
   * Expected format: "rc://src_node_id:src_cluster_id:src_tenant_id@cluster1:count1"
   */
  absl::StatusOr<ReverseConnectionAddress::ReverseConnectionConfig>
  extractReverseConnectionConfig(const envoy::config::core::v3::SocketAddress& socket_address);
};

DECLARE_FACTORY(ReverseConnectionResolver);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

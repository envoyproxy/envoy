#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include <functional>

#include "envoy/network/address.h"

#include "source/common/common/logger.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Custom address type that embeds reverse connection metadata.
 */
class ReverseConnectionAddress : public Network::Address::Instance,
                                 public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
public:
  // Struct to hold reverse connection configuration
  struct ReverseConnectionConfig {
    // Source node id of initiator envoy
    std::string src_node_id;
    // Source cluster id of initiator envoy
    std::string src_cluster_id;
    // Source tenant id of initiator envoy
    std::string src_tenant_id;
    // Remote cluster name of the reverse connection
    std::string remote_cluster;
    // Connection count of the reverse connection
    uint32_t connection_count;
  };

  ReverseConnectionAddress(const ReverseConnectionConfig& config);

  // Network::Address::Instance
  bool operator==(const Instance& rhs) const override;
  Network::Address::Type type() const override {
    return Network::Address::Type::Ip;
  } // Use IP type with our custom IP implementation
  const std::string& asString() const override;
  absl::string_view asStringView() const override;
  const std::string& logicalName() const override;
  const Network::Address::Ip* ip() const override { return ipv4_instance_->ip(); }
  const Network::Address::Pipe* pipe() const override { return nullptr; }
  const Network::Address::EnvoyInternalAddress* envoyInternalAddress() const override {
    return nullptr;
  }
  absl::optional<std::string> networkNamespace() const override { return absl::nullopt; }
  Network::Address::InstanceConstSharedPtr withNetworkNamespace(absl::string_view) const override {
    return nullptr;
  }
  const sockaddr* sockAddr() const override;
  socklen_t sockAddrLen() const override;
  absl::string_view addressType() const override { return "reverse_connection"; }
  const Network::SocketInterface& socketInterface() const override {
    // Return the appropriate reverse connection socket interface for downstream connections
    auto* reverse_socket_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.downstream_socket_interface");
    if (reverse_socket_interface) {
      ENVOY_LOG_MISC(debug, "reverse connection address: using reverse socket interface");
      return *reverse_socket_interface;
    }
    // Fallback to default socket interface if reverse connection interface is not available.
    return Network::SocketInterfaceSingleton::get();
  }

  // Accessor for reverse connection config
  const ReverseConnectionConfig& reverseConnectionConfig() const { return config_; }

private:
  ReverseConnectionConfig config_;
  std::string address_string_;
  std::string logical_name_;
  // Use a regular Ipv4Instance for 127.0.0.1:0
  Network::Address::InstanceConstSharedPtr ipv4_instance_{
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0)};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

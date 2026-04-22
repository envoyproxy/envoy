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
  // Placeholder port for reverse connection listeners. Non-zero to prevent port resolution logic
  // from updating the address, since reverse connection listeners do not actually bind to port.
  static constexpr uint32_t kReverseConnectionListenerPortPlaceholder = 1;

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

  // Minimal IP implementation for reverse connections. Provides the required Ip interface
  // but returns a placeholder port since reverse connection listeners do not actually bind to port.
  class ReverseConnectionIp : public Network::Address::Ip {
  public:
    // Minimal Ipv4 implementation for reverse connections
    class ReverseConnectionIpv4 : public Network::Address::Ipv4 {
    public:
      uint32_t address() const override { return htonl(INADDR_LOOPBACK); } // 127.0.0.1
    };

    const std::string& addressAsString() const override { return address_str_; }
    bool isAnyAddress() const override { return false; }
    bool isUnicastAddress() const override { return true; }
    bool isLinkLocalAddress() const override { return false; }
    bool isUniqueLocalAddress() const override { return false; }
    bool isSiteLocalAddress() const override { return false; }
    bool isTeredoAddress() const override { return false; }
    const Network::Address::Ipv4* ipv4() const override { return &ipv4_; }
    const Network::Address::Ipv6* ipv6() const override { return nullptr; }
    // Return the placeholder port.
    uint32_t port() const override { return kReverseConnectionListenerPortPlaceholder; }
    Network::Address::IpVersion version() const override { return Network::Address::IpVersion::v4; }

    // Public static address string used by both the Ip interface and ReverseConnectionAddress.
    static const std::string address_str_;

  private:
    ReverseConnectionIpv4 ipv4_;
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
  // Return our minimal IP implementation with placeholder port
  const Network::Address::Ip* ip() const override { return &ip_; }
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
  ReverseConnectionIp ip_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

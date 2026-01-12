#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

namespace BootstrapReverseConnection = Envoy::Extensions::Bootstrap::ReverseConnection;

/**
 * Custom address type that uses the UpstreamReverseSocketInterface.
 * This address will be used by RevConHost to ensure socket creation goes through
 * the upstream socket interface.
 */
class UpstreamReverseConnectionAddress
    : public Network::Address::Instance,
      public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
public:
  UpstreamReverseConnectionAddress(const std::string& node_id)
      : node_id_(node_id), address_string_("127.0.0.1:0") {

    // Create a simple socket address for filter chain matching.
    // Use 127.0.0.1:0 which will match the catch-all filter chain
    synthetic_sockaddr_.sin_family = AF_INET;
    synthetic_sockaddr_.sin_port = htons(0);                 // Port 0 for reverse connections
    synthetic_sockaddr_.sin_addr.s_addr = htonl(0x7f000001); // 127.0.0.1
    memset(&synthetic_sockaddr_.sin_zero, 0, sizeof(synthetic_sockaddr_.sin_zero));

    ENVOY_LOG(
        debug,
        "UpstreamReverseConnectionAddress: node: {} using 127.0.0.1:0 for filter chain matching",
        node_id_);
  }

  // Network::Address::Instance.
  bool operator==(const Instance& rhs) const override {
    const auto* other = dynamic_cast<const UpstreamReverseConnectionAddress*>(&rhs);
    return other && node_id_ == other->node_id_;
  }

  Network::Address::Type type() const override { return Network::Address::Type::Ip; }
  const std::string& asString() const override { return address_string_; }
  absl::string_view asStringView() const override { return address_string_; }
  const std::string& logicalName() const override { return node_id_; }
  const Network::Address::Ip* ip() const override { return &ip_; }
  const Network::Address::Pipe* pipe() const override { return nullptr; }
  const Network::Address::EnvoyInternalAddress* envoyInternalAddress() const override {
    return nullptr;
  }
  const sockaddr* sockAddr() const override {
    return reinterpret_cast<const sockaddr*>(&synthetic_sockaddr_);
  }
  socklen_t sockAddrLen() const override { return sizeof(synthetic_sockaddr_); }
  // Set to default so that the default client connection factory is used to initiate connections
  // to. the address.
  absl::string_view addressType() const override { return "default"; }
  absl::optional<std::string> networkNamespace() const override { return absl::nullopt; }
  Network::Address::InstanceConstSharedPtr withNetworkNamespace(absl::string_view) const override {
    return nullptr;
  }

  // Override socketInterface to use the ReverseTunnelAcceptor.
  const Network::SocketInterface& socketInterface() const override {
    ENVOY_LOG(debug, "UpstreamReverseConnectionAddress: socketInterface() called for node: {}",
              node_id_);
    auto* upstream_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    if (upstream_interface) {
      ENVOY_LOG(debug, "UpstreamReverseConnectionAddress: Using ReverseTunnelAcceptor for node: {}",
                node_id_);
      return *upstream_interface;
    }
    // Fallback to default socket interface if upstream interface is not available.
    return *Network::socketInterface(
        "envoy.extensions.network.socket_interface.default_socket_interface");
  }

private:
  // Simple IPv4 implementation for upstream reverse connection addresses.
  struct UpstreamReverseConnectionIp : public Network::Address::Ip {
    const std::string& addressAsString() const override { return address_string_; }
    bool isAnyAddress() const override { return true; }
    bool isUnicastAddress() const override { return false; }
    const Network::Address::Ipv4* ipv4() const override { return nullptr; }
    const Network::Address::Ipv6* ipv6() const override { return nullptr; }
    uint32_t port() const override { return 0; }
    Network::Address::IpVersion version() const override { return Network::Address::IpVersion::v4; }

    // Additional pure virtual methods that need implementation.
    bool isLinkLocalAddress() const override { return false; }
    bool isUniqueLocalAddress() const override { return false; }
    bool isSiteLocalAddress() const override { return false; }
    bool isTeredoAddress() const override { return false; }

    std::string address_string_{"0.0.0.0:0"};
  };

  std::string node_id_;
  std::string address_string_;
  UpstreamReverseConnectionIp ip_;
  struct sockaddr_in synthetic_sockaddr_; // Socket address for filter chain matching
};

/**
 * The RevConCluster is a dynamic cluster that automatically adds hosts using
 * request context of the downstream connection. Later, these hosts are used
 * to retrieve reverse connection sockets to stream data to upstream endpoints.
 * Also, the RevConCluster cleans these hosts if no connection pool is using them.
 */
class RevConCluster : public Upstream::ClusterImplBase {
  friend class ReverseConnectionClusterTest;

public:
  RevConCluster(
      const envoy::config::cluster::v3::Cluster& config, Upstream::ClusterFactoryContext& context,
      absl::Status& creation_status,
      const envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig&
          rev_con_config);

  ~RevConCluster() override { cleanup_timer_->disableTimer(); }

  // Upstream::Cluster.
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

  class LoadBalancer : public Upstream::LoadBalancer {
  public:
    LoadBalancer(const std::shared_ptr<RevConCluster>& parent) : parent_(parent) {}

    // Chooses a host to send a downstream request over a reverse connection endpoint.
    // The request MUST provide a host identifier via dynamic metadata populated by a matcher
    // action. No header or authority/SNI fallbacks are used.
    Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;

    // Virtual functions that are not supported by our custom load-balancer.
    Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
      return nullptr;
    }
    absl::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                             const Upstream::Host& /*host*/,
                             std::vector<uint8_t>& /*hash_key*/) override {
      return absl::nullopt;
    }

    // Lifetime tracking not implemented.
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }

  private:
    const std::shared_ptr<RevConCluster> parent_;
  };

private:
  struct LoadBalancerFactory : public Upstream::LoadBalancerFactory {
    LoadBalancerFactory(const std::shared_ptr<RevConCluster>& cluster) : cluster_(cluster) {}

    // Upstream::LoadBalancerFactory.
    Upstream::LoadBalancerPtr create() { return std::make_unique<LoadBalancer>(cluster_); }
    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override { return create(); }

    const std::shared_ptr<RevConCluster> cluster_;
  };

  struct ThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
    ThreadAwareLoadBalancer(const std::shared_ptr<RevConCluster>& cluster) : cluster_(cluster) {}

    // Upstream::ThreadAwareLoadBalancer.
    Upstream::LoadBalancerFactorySharedPtr factory() override {
      return std::make_shared<LoadBalancerFactory>(cluster_);
    }
    absl::Status initialize() override { return absl::OkStatus(); }

    const std::shared_ptr<RevConCluster> cluster_;
  };

  // Periodically cleans the stale hosts from host_map_.
  void cleanup();

  // Checks if a host exists for a given host identifier and if not creates and caches it.
  Upstream::HostSelectionResponse checkAndCreateHost(absl::string_view host_id);

  // Get the upstream socket manager from the thread-local registry.
  BootstrapReverseConnection::UpstreamSocketManager* getUpstreamSocketManager() const;

  // No pre-initialize work needs to be completed by REVERSE CONNECTION cluster.
  void startPreInit() override { onPreInitComplete(); }

  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds cleanup_interval_;
  Event::TimerPtr cleanup_timer_;
  absl::Mutex host_map_lock_;
  absl::flat_hash_map<std::string, Upstream::HostSharedPtr> host_map_;
  // Formatter for computing host identifier from request context.
  Envoy::Formatter::FormatterPtr host_id_formatter_;
  friend class RevConClusterFactory;
};

using RevConClusterSharedPtr = std::shared_ptr<RevConCluster>;

class RevConClusterFactory
    : public Upstream::ConfigurableClusterFactoryBase<
          envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig> {
public:
  RevConClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.reverse_connection") {}

private:
  friend class ReverseConnectionClusterTest;
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig&
          proto_config,
      Upstream::ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(RevConClusterFactory);

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy

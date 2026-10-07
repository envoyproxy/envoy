#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "envoy/common/platform.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.validate.h"
#include "envoy/upstream/admin_endpoint_provider.h"

#include "source/common/common/logger.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
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
      : node_id_(node_id), ipv4_instance_("127.0.0.1", /*port=*/uint32_t{0}) {
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

  Network::Address::Type type() const override { return ipv4_instance_.type(); }
  const std::string& asString() const override { return ipv4_instance_.asString(); }
  absl::string_view asStringView() const override { return ipv4_instance_.asStringView(); }
  const std::string& logicalName() const override { return node_id_; }
  // Delegate the IP and sockaddr accessors to a real Ipv4Instance so any caller that inspects
  // ip()->ipv4() or copies sockAddr() sees a concrete loopback address rather than a partially
  // populated one.
  const Network::Address::Ip* ip() const override { return ipv4_instance_.ip(); }
  const Network::Address::Pipe* pipe() const override { return nullptr; }
  const Network::Address::EnvoyInternalAddress* envoyInternalAddress() const override {
    return nullptr;
  }
  const sockaddr* sockAddr() const override { return ipv4_instance_.sockAddr(); }
  socklen_t sockAddrLen() const override { return ipv4_instance_.sockAddrLen(); }
  // Set to default so that the default client connection factory is used to initiate connections
  // to. the address.
  absl::string_view addressType() const override { return "default"; }
  std::optional<std::string> networkNamespace() const override { return std::nullopt; }
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
  std::string node_id_;
  // Concrete loopback address backing the synthetic reverse connection address.
  Network::Address::Ipv4Instance ipv4_instance_;
};

class RevConCluster;

// Holds the shared_ptr to the RevConCluster for the thread-aware load balancer and its per-worker
// load balancers. On destruction it posts the final cluster release to the cluster's main-thread
// dispatcher, so the cluster and its main-thread cleanup timer are always destroyed on the main
// thread even when the last worker reference is dropped during cluster removal.
class RevConClusterHandle {
public:
  explicit RevConClusterHandle(std::shared_ptr<RevConCluster> cluster)
      : cluster_(std::move(cluster)) {}
  ~RevConClusterHandle();

  std::shared_ptr<RevConCluster> cluster_;
};

using RevConClusterHandleSharedPtr = std::shared_ptr<RevConClusterHandle>;

/**
 * The RevConCluster is a dynamic cluster that automatically adds hosts using
 * request context of the downstream connection. Later, these hosts are used
 * to retrieve reverse connection sockets to stream data to upstream endpoints.
 * Also, the RevConCluster cleans these hosts if no connection pool is using them.
 */
class RevConCluster : public Upstream::ClusterImplBase, public Upstream::AdminEndpointProvider {
  friend class ReverseConnectionClusterTest;
  friend class RevConClusterHandle;

public:
  RevConCluster(
      const envoy::config::cluster::v3::Cluster& config, Upstream::ClusterFactoryContext& context,
      absl::Status& creation_status,
      const envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig&
          rev_con_config);

  ~RevConCluster() override { cleanup_timer_->disableTimer(); }

  // Upstream::Cluster.
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

  // Upstream::Cluster. This cluster provides its own admin endpoints.
  const Upstream::AdminEndpointProvider* adminEndpointProvider() const override { return this; }

  // Upstream::AdminEndpointProvider. Surfaces currently-reachable reverse-tunnel nodes on /clusters
  // without creating load-balanced hosts.
  std::vector<Upstream::AdminEndpointProvider::AdminEndpoint> adminEndpoints() const override;

  class LoadBalancer : public Upstream::LoadBalancer {
  public:
    LoadBalancer(const RevConClusterHandleSharedPtr& parent) : parent_(parent) {}

    // Chooses a host to send a downstream request over a reverse connection endpoint.
    // The request MUST provide a host identifier via dynamic metadata populated by a matcher
    // action. No header or authority/SNI fallbacks are used.
    Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;

    // Virtual functions that are not supported by our custom load-balancer.
    Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
      return nullptr;
    }
    std::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                             const Upstream::Host& /*host*/,
                             std::vector<uint8_t>& /*hash_key*/) override {
      return std::nullopt;
    }

    // Lifetime tracking not implemented.
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }

  private:
    const RevConClusterHandleSharedPtr parent_;
  };

private:
  struct LoadBalancerFactory : public Upstream::LoadBalancerFactory {
    LoadBalancerFactory(const RevConClusterHandleSharedPtr& cluster) : cluster_(cluster) {}

    // Upstream::LoadBalancerFactory.
    Upstream::LoadBalancerPtr create() { return std::make_unique<LoadBalancer>(cluster_); }
    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override { return create(); }
    bool recreateOnHostChangeDeprecated() const override { return false; }

    const RevConClusterHandleSharedPtr cluster_;
  };

  struct ThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
    ThreadAwareLoadBalancer(const RevConClusterHandleSharedPtr& cluster) : cluster_(cluster) {}

    // Upstream::ThreadAwareLoadBalancer.
    Upstream::LoadBalancerFactorySharedPtr factory() override {
      return std::make_shared<LoadBalancerFactory>(cluster_);
    }
    absl::Status initialize() override { return absl::OkStatus(); }

    const RevConClusterHandleSharedPtr cluster_;
  };

  // Periodically cleans the stale hosts from host_map_.
  void cleanup();

  // Checks if a host exists for a given host identifier and if not creates and caches it.
  Upstream::HostSelectionResponse checkAndCreateHost(absl::string_view host_id,
                                                     Upstream::HostSharedPtr& created_host);

  void addHostToHostSet(Upstream::HostSharedPtr host);

  // Get the upstream socket manager from the thread-local registry.
  BootstrapReverseConnection::UpstreamSocketManager* getUpstreamSocketManager() const;

  // Get the process-wide acceptor extension (for cross-worker stats). Returns nullptr if the
  // bootstrap extension is not configured. Safe to call from the main/admin thread (does not
  // require thread-local state).
  BootstrapReverseConnection::ReverseTunnelAcceptorExtension* getAcceptorExtension() const;

  // No pre-initialize work needs to be completed by REVERSE CONNECTION cluster.
  void startPreInit() override { onPreInitComplete(); }

  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds cleanup_interval_;
  Event::TimerPtr cleanup_timer_;
  // Mutable so the const adminEndpoints() accessor can read host_map_ under the lock.
  mutable absl::Mutex host_map_lock_;
  absl::flat_hash_map<std::string, Upstream::HostSharedPtr> host_map_;
  // Formatter for computing host identifier from request context.
  Envoy::Formatter::FormatterPtr host_id_formatter_;
  // Optional formatter for computing tenant identifier from request context.
  // Used when tenant isolation is enabled to create tenant-scoped identifiers.
  Envoy::Formatter::FormatterPtr tenant_id_formatter_;
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

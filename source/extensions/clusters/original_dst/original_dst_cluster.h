#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

class OriginalDstClusterFactory;
class OriginalDstClusterTest;

struct HostsForAddress {
  HostsForAddress(HostSharedPtr& host) : host_(host), used_(true) {}

  // Primary host for the address. This is set by the first worker that posts
  // to the main to add a host. The field is read by all workers.
  const HostSharedPtr host_;
  // Hosts that are added concurrently with host_ are stored in this list.
  // This is populated by the subsequent workers that have not received the
  // updated table with set host_. The field is only accessed from the main
  // thread.
  std::vector<HostSharedPtr> hosts_;
  // Marks as recently used by load balancers.
  std::atomic<bool> used_;
};

using HostsForAddressSharedPtr = std::shared_ptr<HostsForAddress>;
using HostMultiMap = absl::flat_hash_map<std::string, HostsForAddressSharedPtr>;
using HostMultiMapSharedPtr = std::shared_ptr<HostMultiMap>;
using HostMultiMapConstSharedPtr = std::shared_ptr<const HostMultiMap>;

class OriginalDstCluster;

// Handle object whose sole purpose is to ensure that the destructor of the inner OriginalDstCluster
// is called on the main thread.
class OriginalDstClusterHandle {
public:
  OriginalDstClusterHandle(std::shared_ptr<OriginalDstCluster> cluster)
      : cluster_(std::move(cluster)) {}
  ~OriginalDstClusterHandle();

private:
  std::shared_ptr<OriginalDstCluster> cluster_;
  friend class OriginalDstCluster;
};

using OriginalDstClusterHandleSharedPtr = std::shared_ptr<OriginalDstClusterHandle>;

/**
 * The OriginalDstCluster is a dynamic cluster that automatically adds hosts as needed based on the
 * original destination address of the downstream connection. These hosts are also automatically
 * cleaned up after they have not seen traffic for a configurable cleanup interval time
 * ("cleanup_interval_ms").
 */
class OriginalDstCluster : public ClusterImplBase {
public:
  ~OriginalDstCluster() override {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    cleanup_timer_->disableTimer();
  }

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

  /**
   * Special Load Balancer for Original Dst Cluster.
   *
   * Load balancer gets called with the downstream context which can be used to make sure the
   * Original Dst cluster has a Host for the original destination. Normally load balancers can't
   * modify clusters, but in this case we access a singleton OriginalDstCluster that we can ask to
   * add hosts on demand. Additions are synced with all other threads so that the host set in the
   * cluster remains (eventually) consistent. If multiple threads add a host to the same upstream
   * address then two distinct HostSharedPtr's (with the same upstream IP address) will be added,
   * and both of them will eventually time out.
   */
  class LoadBalancer : public Upstream::LoadBalancer {
  public:
    LoadBalancer(const OriginalDstClusterHandleSharedPtr& parent)
        : parent_(parent), http_header_name_(parent->cluster_->httpHeaderName()),
          metadata_key_(parent->cluster_->metadataKey()),
          port_override_(parent->cluster_->portOverride()),
          host_map_(parent->cluster_->getCurrentHostMap()) {}

    // Upstream::LoadBalancer
    HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;
    // Preconnecting is not implemented for OriginalDstCluster
    HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { return nullptr; }
    // Pool selection not implemented for OriginalDstCluster
    absl::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                             const Upstream::Host& /*host*/,
                             std::vector<uint8_t>& /*hash_key*/) override {
      return absl::nullopt;
    }
    // Lifetime tracking not implemented for OriginalDstCluster
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }

    Network::Address::InstanceConstSharedPtr filterStateOverrideHost(LoadBalancerContext* context);
    Network::Address::InstanceConstSharedPtr requestOverrideHost(LoadBalancerContext* context);
    Network::Address::InstanceConstSharedPtr metadataOverrideHost(LoadBalancerContext* context);

  private:
    const OriginalDstClusterHandleSharedPtr parent_;
    // The optional original host provider that extracts the address from HTTP header map.
    const absl::optional<Http::LowerCaseString>& http_header_name_;
    const absl::optional<Config::MetadataKey>& metadata_key_;
    const absl::optional<uint32_t> port_override_;
    HostMultiMapConstSharedPtr host_map_;
  };

  const absl::optional<Http::LowerCaseString>& httpHeaderName() { return http_header_name_; }
  const absl::optional<Config::MetadataKey>& metadataKey() { return metadata_key_; }
  const absl::optional<uint32_t> portOverride() { return port_override_; }

private:
  friend class OriginalDstClusterFactory;
  friend class OriginalDstClusterTest;
  OriginalDstCluster(const envoy::config::cluster::v3::Cluster& config,
                     ClusterFactoryContext& context);

  struct LoadBalancerFactory : public Upstream::LoadBalancerFactory {
    LoadBalancerFactory(const OriginalDstClusterHandleSharedPtr& cluster) : cluster_(cluster) {}

    // Upstream::LoadBalancerFactory
    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override {
      return std::make_unique<LoadBalancer>(cluster_);
    }

    const OriginalDstClusterHandleSharedPtr cluster_;
  };

  struct ThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
    ThreadAwareLoadBalancer(const OriginalDstClusterHandleSharedPtr& cluster) : cluster_(cluster) {}

    // Upstream::ThreadAwareLoadBalancer
    Upstream::LoadBalancerFactorySharedPtr factory() override {
      return std::make_shared<LoadBalancerFactory>(cluster_);
    }
    void initialize() override {}

    const OriginalDstClusterHandleSharedPtr cluster_;
  };

  HostMultiMapConstSharedPtr getCurrentHostMap() {
    absl::ReaderMutexLock lock(&host_map_lock_);
    return host_map_;
  }

  void setHostMap(const HostMultiMapConstSharedPtr& new_host_map) {
    absl::WriterMutexLock lock(&host_map_lock_);
    host_map_ = new_host_map;
  }

  void addHost(HostSharedPtr&);
  void cleanup();

  // ClusterImplBase
  void startPreInit() override { onPreInitComplete(); }

  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds cleanup_interval_ms_;
  Event::TimerPtr cleanup_timer_;

  absl::Mutex host_map_lock_;
  HostMultiMapConstSharedPtr host_map_ ABSL_GUARDED_BY(host_map_lock_);
  absl::optional<Http::LowerCaseString> http_header_name_;
  absl::optional<Config::MetadataKey> metadata_key_;
  absl::optional<uint32_t> port_override_;
  friend class OriginalDstClusterFactory;
  friend class OriginalDstClusterHandle;
};

constexpr absl::string_view OriginalDstClusterFilterStateKey =
    "envoy.network.transport_socket.original_dst_address";

class OriginalDstClusterFactory : public ClusterFactoryImplBase {
public:
  OriginalDstClusterFactory() : ClusterFactoryImplBase("envoy.cluster.original_dst") {}

private:
  friend class OriginalDstClusterTest;
  absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
  createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(OriginalDstClusterFactory);

} // namespace Upstream
} // namespace Envoy

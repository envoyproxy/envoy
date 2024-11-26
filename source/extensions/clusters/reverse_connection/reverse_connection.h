#pragma once

/*
 * Copyright (c) 2024 Nutanix Inc. All rights reserved.
 *
 * Author: abhinav.agarwal@nutanix.com
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * The RevConCluster is a dynamic cluster that automatically adds hosts using
 * request context of the downstream connection. Later, these hosts are used
 * to retrieve reverse connection sockets to stream data to upstream endpoints.
 * Also, the RevConCluster cleans these hosts if no connection pool is using them.
 */
class RevConCluster : public ClusterImplBase {
public:
  RevConCluster(const envoy::config::cluster::v3::Cluster& config, ClusterFactoryContext& context,
                absl::Status& creation_status);

  ~RevConCluster() override { cleanup_timer_->disableTimer(); }

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

  class LoadBalancer : public Upstream::LoadBalancer {
  public:
    LoadBalancer(const std::shared_ptr<RevConCluster>& parent) : parent_(parent) {}

    HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

    // Virtual functions that are not supported by our custom load-balancer.
    HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { return nullptr; }
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

    // Upstream::LoadBalancerFactory
    Upstream::LoadBalancerPtr create() { return std::make_unique<LoadBalancer>(cluster_); }
    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override { return create(); }

    const std::shared_ptr<RevConCluster> cluster_;
  };

  struct ThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
    ThreadAwareLoadBalancer(const std::shared_ptr<RevConCluster>& cluster) : cluster_(cluster) {}

    // Upstream::ThreadAwareLoadBalancer
    Upstream::LoadBalancerFactorySharedPtr factory() override {
      return std::make_shared<LoadBalancerFactory>(cluster_);
    }
    absl::Status initialize() override { return absl::OkStatus(); }

    const std::shared_ptr<RevConCluster> cluster_;
  };

  // Periodically cleans the stale hosts from host_map_.
  void cleanup();

  // Checks if a host exists for a given `host_id` and if not it creates and caches
  // that host to the map.
  HostSharedPtr checkAndCreateHost(const absl::string_view host_id);

  // Checks if the request headers contain any header that hold host_id value.
  // If such header is present, it return that header value.
  absl::string_view getHostIdValue(const Http::RequestHeaderMap* request_headers);

  // No pre-initialize work needs to be completed by REVERSE CONNECTION cluster.
  void startPreInit() override { onPreInitComplete(); }

  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds cleanup_interval_;
  std::string default_host_id_;
  Event::TimerPtr cleanup_timer_;
  absl::Mutex host_map_lock_;
  absl::flat_hash_map<std::string, HostSharedPtr> host_map_;
  std::vector<absl::optional<Http::LowerCaseString>> http_header_names_;
  friend class RevConClusterFactory;
};

using RevConClusterSharedPtr = std::shared_ptr<RevConCluster>;

class RevConClusterFactory : public ClusterFactoryImplBase {
public:
  RevConClusterFactory() : ClusterFactoryImplBase("envoy.cluster.reverse_connection") {}

  absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
  createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context) override;

private:
};

} // namespace Upstream
} // namespace Envoy

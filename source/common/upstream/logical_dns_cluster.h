#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "envoy/stats/scope.h"

#include "common/common/empty_string.h"
#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/logical_host.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/well_known_names.h"

namespace Envoy {
namespace Upstream {

/**
 * The LogicalDnsCluster is a type of cluster that creates a single logical host that wraps
 * an async DNS resolver. The DNS resolver will continuously resolve, and swap in the first IP
 * address in the resolution set. However the logical owning host will not change. Any created
 * connections against this host will use the currently resolved IP. This means that a connection
 * pool using the logical host may end up with connections to many different real IPs.
 *
 * This cluster type is useful for large web services that use DNS in a round robin fashion, such
 * that DNS resolution may repeatedly return different results. A single connection pool can be
 * created that will internally have connections to different backends, while still allowing long
 * connection lengths and keep alive. The cluster type should only be used when an IP address change
 * means that connections using the IP should not drain.
 */
class LogicalDnsCluster : public ClusterImplBase {
public:
  LogicalDnsCluster(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                    Network::DnsResolverSharedPtr dns_resolver,
                    Server::Configuration::TransportSocketFactoryContext& factory_context,
                    Stats::ScopePtr&& stats_scope, bool added_via_api);

  ~LogicalDnsCluster() override;

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  const envoy::api::v2::endpoint::LocalityLbEndpoints& localityLbEndpoint() const {
    // This is checked in the constructor, i.e. at config load time.
    ASSERT(load_assignment_.endpoints().size() == 1);
    return load_assignment_.endpoints()[0];
  }

  const envoy::api::v2::endpoint::LbEndpoint& lbEndpoint() const {
    // This is checked in the constructor, i.e. at config load time.
    ASSERT(localityLbEndpoint().lb_endpoints().size() == 1);
    return localityLbEndpoint().lb_endpoints()[0];
  }

  template <typename AddressInstance>
  void updateHosts(
      std::string dns_address, const std::list<DnsResponse>&& response,
      std::function<Network::Address::InstanceConstSharedPtr(const AddressInstance&)> translate) {
    active_dns_query_ = nullptr;
    ENVOY_LOG(debug, "async DNS resolution complete for {}", dns_address);
    info_->stats().update_success_.inc();

    std::chrono::milliseconds refresh_rate = dns_refresh_rate_ms_;
    if (!response.empty()) {
      // TODO(mattklein123): Move port handling into the DNS interface.
      ASSERT(response.front().address_ != nullptr);
      Network::Address::InstanceConstSharedPtr new_address = translate(response.front().address_);

      if (respect_dns_ttl_ && response.front().ttl_ != std::chrono::seconds(0)) {
        refresh_rate = response.front().ttl_;
      }

      if (!logical_host_) {
        logical_host_.reset(
            new LogicalHost(info_, hostname_, new_address, localityLbEndpoint(), lbEndpoint()));

        const auto& locality_lb_endpoint = localityLbEndpoint();
        PriorityStateManager priority_state_manager(*this, local_info_, nullptr);
        priority_state_manager.initializePriorityFor(locality_lb_endpoint);
        priority_state_manager.registerHostForPriority(logical_host_, locality_lb_endpoint);

        const uint32_t priority = locality_lb_endpoint.priority();
        priority_state_manager.updateClusterPrioritySet(
            priority, std::move(priority_state_manager.priorityState()[priority].first),
            absl::nullopt, absl::nullopt, absl::nullopt);
      }

      if (!current_resolved_address_ || !(*new_address == *current_resolved_address_)) {
        current_resolved_address_ = new_address;

        // Make sure that we have an updated address for admin display, health
        // checking, and creating real host connections.
        logical_host_->setNewAddress(new_address, lbEndpoint());
      }
    }

    onPreInitComplete();
    resolve_timer_->enableTimer(refresh_rate);
  }

  void startResolve();

  // ClusterImplBase
  void startPreInit() override;

  Network::DnsResolverSharedPtr dns_resolver_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  const bool respect_dns_ttl_;
  Network::DnsLookupFamily dns_lookup_family_;
  Event::TimerPtr resolve_timer_;
  std::string dns_url_;
  std::string hostname_;
  bool srv_{false};
  Network::Address::InstanceConstSharedPtr current_resolved_address_;
  LogicalHostSharedPtr logical_host_;
  Network::ActiveDnsQuery* active_dns_query_{};
  const LocalInfo::LocalInfo& local_info_;
  const envoy::api::v2::ClusterLoadAssignment load_assignment_;
};

class LogicalDnsClusterFactory : public ClusterFactoryImplBase {
public:
  LogicalDnsClusterFactory()
      : ClusterFactoryImplBase(Extensions::Clusters::ClusterTypes::get().LogicalDns) {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
  createClusterImpl(const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
                    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
                    Stats::ScopePtr&& stats_scope) override;
};

DECLARE_FACTORY(LogicalDnsClusterFactory);

} // namespace Upstream
} // namespace Envoy

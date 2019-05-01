#pragma once

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Implementation of Upstream::Cluster that does periodic DNS resolution and updates the host
 * member set if the DNS members change.
 */
class StrictDnsClusterImpl : public BaseDynamicClusterImpl {
public:
  StrictDnsClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                       Network::DnsResolverSharedPtr dns_resolver,
                       Server::Configuration::TransportSocketFactoryContext& factory_context,
                       Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  struct ResolveTarget {
    ResolveTarget(StrictDnsClusterImpl& parent, Event::Dispatcher& dispatcher,
                  const std::string& url,
                  const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
                  const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint);
    ~ResolveTarget();

    template <typename AddressInstance>
    void updateHosts(
        const std::list<DnsResponse>&& response,
        std::function<Network::Address::InstanceConstSharedPtr(const AddressInstance&)> translate) {
      active_query_ = nullptr;
      ENVOY_LOG(trace, "async DNS resolution complete for {}", dns_address_);
      parent_.info_->stats().update_success_.inc();

      std::unordered_map<std::string, HostSharedPtr> updated_hosts;
      HostVector new_hosts;
      std::chrono::seconds ttl_refresh_rate = std::chrono::seconds::max();
      for (const auto& resp : response) {
        ASSERT(resp.address_ != nullptr);
        new_hosts.emplace_back(new HostImpl(
            parent_.info_, dns_address_, translate(resp.address_), lb_endpoint_.metadata(),
            lb_endpoint_.load_balancing_weight().value(), locality_lb_endpoint_.locality(),
            lb_endpoint_.endpoint().health_check_config(), locality_lb_endpoint_.priority(),
            lb_endpoint_.health_status()));
        ttl_refresh_rate = min(ttl_refresh_rate, resp.ttl_);
      }

      HostVector hosts_added;
      HostVector hosts_removed;
      if (parent_.updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed,
                                        updated_hosts, all_hosts_)) {
        ENVOY_LOG(debug, "DNS hosts have changed for {}", dns_address_);
        ASSERT(std::all_of(hosts_.begin(), hosts_.end(), [&](const auto& host) {
          return host->priority() == locality_lb_endpoint_.priority();
        }));
        parent_.updateAllHosts(hosts_added, hosts_removed, locality_lb_endpoint_.priority());
      } else {
        parent_.info_->stats().update_no_rebuild_.inc();
      }

      all_hosts_ = std::move(updated_hosts);

      // If there is an initialize callback, fire it now. Note that if the cluster refers to
      // multiple DNS names, this will return initialized after a single DNS resolution
      // completes. This is not perfect but is easier to code and unclear if the extra
      // complexity is needed so will start with this.
      parent_.onPreInitComplete();

      std::chrono::milliseconds final_refresh_rate = parent_.dns_refresh_rate_ms_;

      if (parent_.respect_dns_ttl_ && ttl_refresh_rate != std::chrono::seconds(0)) {
        final_refresh_rate = ttl_refresh_rate;
        ENVOY_LOG(debug, "DNS refresh rate reset for {}, refresh rate {} ms", dns_address_,
                  final_refresh_rate.count());
      }

      resolve_timer_->enableTimer(final_refresh_rate);
    }

    void startResolve();

    StrictDnsClusterImpl& parent_;
    Network::ActiveDnsQuery* active_query_{};
    std::string dns_address_;
    bool srv_;
    uint32_t port_;
    Event::TimerPtr resolve_timer_;
    HostVector hosts_;
    const envoy::api::v2::endpoint::LocalityLbEndpoints locality_lb_endpoint_;
    const envoy::api::v2::endpoint::LbEndpoint lb_endpoint_;
    HostMap all_hosts_;
  };

  using ResolveTargetPtr = std::unique_ptr<ResolveTarget>;

  void updateAllHosts(const HostVector& hosts_added, const HostVector& hosts_removed,
                      uint32_t priority);

  // ClusterImplBase
  void startPreInit() override;

  const LocalInfo::LocalInfo& local_info_;
  Network::DnsResolverSharedPtr dns_resolver_;
  std::list<ResolveTargetPtr> resolve_targets_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  const bool respect_dns_ttl_;
  Network::DnsLookupFamily dns_lookup_family_;
  uint32_t overprovisioning_factor_;
};

/**
 * Factory for StrictDnsClusterImpl
 */
class StrictDnsClusterFactory : public ClusterFactoryImplBase {
public:
  StrictDnsClusterFactory()
      : ClusterFactoryImplBase(Extensions::Clusters::ClusterTypes::get().StrictDns) {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
  createClusterImpl(const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
                    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
                    Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy

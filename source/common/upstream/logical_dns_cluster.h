#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/empty_string.h"
#include "common/upstream/upstream_impl.h"

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
                    Network::DnsResolverSharedPtr dns_resolver, ThreadLocal::SlotAllocator& tls,
                    Server::Configuration::TransportSocketFactoryContext& factory_context,
                    Stats::ScopePtr&& stats_scope, bool added_via_api);

  ~LogicalDnsCluster();

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  struct LogicalHost : public HostImpl {
    LogicalHost(ClusterInfoConstSharedPtr cluster, const std::string& hostname,
                Network::Address::InstanceConstSharedPtr address, LogicalDnsCluster& parent)
        : HostImpl(cluster, hostname, address, parent.lbEndpoint().metadata(),
                   parent.lbEndpoint().load_balancing_weight().value(),
                   parent.localityLbEndpoint().locality(),
                   parent.lbEndpoint().endpoint().health_check_config()),
          parent_(parent) {}

    // Upstream::Host
    CreateConnectionData
    createConnection(Event::Dispatcher& dispatcher,
                     const Network::ConnectionSocket::OptionsSharedPtr& options) const override;

    LogicalDnsCluster& parent_;
  };

  struct RealHostDescription : public HostDescription {
    RealHostDescription(Network::Address::InstanceConstSharedPtr address,
                        const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
                        const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint,
                        HostConstSharedPtr logical_host)
        : address_(address), logical_host_(logical_host),
          metadata_(std::make_shared<envoy::api::v2::core::Metadata>(lb_endpoint.metadata())),
          health_check_address_(
              lb_endpoint.endpoint().health_check_config().port_value() == 0
                  ? address
                  : Network::Utility::getAddressWithPort(
                        *address, lb_endpoint.endpoint().health_check_config().port_value())),
          locality_lb_endpoint_(locality_lb_endpoint), lb_endpoint_(lb_endpoint) {}

    // Upstream:HostDescription
    bool canary() const override { return false; }
    void canary(bool) override {}
    const std::shared_ptr<envoy::api::v2::core::Metadata> metadata() const override {
      return metadata_;
    }
    void metadata(const envoy::api::v2::core::Metadata&) override {}

    const ClusterInfo& cluster() const override { return logical_host_->cluster(); }
    HealthCheckHostMonitor& healthChecker() const override {
      return logical_host_->healthChecker();
    }
    Outlier::DetectorHostMonitor& outlierDetector() const override {
      return logical_host_->outlierDetector();
    }
    const HostStats& stats() const override { return logical_host_->stats(); }
    const std::string& hostname() const override { return logical_host_->hostname(); }
    Network::Address::InstanceConstSharedPtr address() const override { return address_; }
    const envoy::api::v2::core::Locality& locality() const override {
      return locality_lb_endpoint_.locality();
    }
    Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
      return health_check_address_;
    }
    uint32_t priority() const { return locality_lb_endpoint_.priority(); }
    Network::Address::InstanceConstSharedPtr address_;
    HostConstSharedPtr logical_host_;
    const std::shared_ptr<envoy::api::v2::core::Metadata> metadata_;
    Network::Address::InstanceConstSharedPtr health_check_address_;
    const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint_;
    const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint_;
  };

  struct PerThreadCurrentHostData : public ThreadLocal::ThreadLocalObject {
    Network::Address::InstanceConstSharedPtr current_resolved_address_;
  };

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

  void startResolve();

  // ClusterImplBase
  void startPreInit() override;

  Network::DnsResolverSharedPtr dns_resolver_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  Network::DnsLookupFamily dns_lookup_family_;
  ThreadLocal::SlotPtr tls_;
  Event::TimerPtr resolve_timer_;
  std::string dns_url_;
  std::string hostname_;
  Network::Address::InstanceConstSharedPtr current_resolved_address_;
  HostSharedPtr logical_host_;
  Network::ActiveDnsQuery* active_dns_query_{};
  const LocalInfo::LocalInfo& local_info_;
  const envoy::api::v2::ClusterLoadAssignment load_assignment_;
};

} // namespace Upstream
} // namespace Envoy

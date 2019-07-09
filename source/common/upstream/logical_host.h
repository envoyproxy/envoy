#pragma once

#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * A host implementation that can have its address changed in order to create temporal "real"
 * hosts.
 */
class LogicalHost : public HostImpl {
public:
  LogicalHost(const ClusterInfoConstSharedPtr& cluster, const std::string& hostname,
              const Network::Address::InstanceConstSharedPtr& address,
              const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
              const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint)
      : HostImpl(cluster, hostname, address, lb_endpoint.metadata(),
                 lb_endpoint.load_balancing_weight().value(), locality_lb_endpoint.locality(),
                 lb_endpoint.endpoint().health_check_config(), locality_lb_endpoint.priority(),
                 lb_endpoint.health_status()) {}

  // Set the new address. Updates are typically rare so a R/W lock is used for address updates.
  // Note that the health check address update requires no lock to be held since it is only
  // used on the main thread, but we do so anyway since it shouldn't be perf critical and will
  // future proof the code.
  void setNewAddress(const Network::Address::InstanceConstSharedPtr& address,
                     const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint) {
    const auto& port_value = lb_endpoint.endpoint().health_check_config().port_value();
    auto health_check_address =
        port_value == 0 ? address : Network::Utility::getAddressWithPort(*address, port_value);

    absl::WriterMutexLock lock(&address_lock_);
    address_ = address;
    health_check_address_ = health_check_address;
  }

  // Upstream::Host
  CreateConnectionData createConnection(
      Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
      Network::TransportSocketOptionsSharedPtr transport_socket_options) const override;

  // Upstream::HostDescription
  Network::Address::InstanceConstSharedPtr address() const override {
    absl::ReaderMutexLock lock(&address_lock_);
    return HostImpl::address();
  }
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    absl::ReaderMutexLock lock(&address_lock_);
    return HostImpl::healthCheckAddress();
  }

private:
  mutable absl::Mutex address_lock_;
};

using LogicalHostSharedPtr = std::shared_ptr<LogicalHost>;

/**
 * A real host that forwards most of its calls to a logical host, but returns a snapped address.
 */
class RealHostDescription : public HostDescription {
public:
  RealHostDescription(Network::Address::InstanceConstSharedPtr address,
                      HostConstSharedPtr logical_host)
      : address_(address), logical_host_(logical_host) {}

  // Upstream:HostDescription
  bool canary() const override { return logical_host_->canary(); }
  void canary(bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  const std::shared_ptr<envoy::api::v2::core::Metadata> metadata() const override {
    return logical_host_->metadata();
  }
  void metadata(const envoy::api::v2::core::Metadata&) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  const ClusterInfo& cluster() const override { return logical_host_->cluster(); }
  HealthCheckHostMonitor& healthChecker() const override { return logical_host_->healthChecker(); }
  Outlier::DetectorHostMonitor& outlierDetector() const override {
    return logical_host_->outlierDetector();
  }
  const HostStats& stats() const override { return logical_host_->stats(); }
  const std::string& hostname() const override { return logical_host_->hostname(); }
  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const envoy::api::v2::core::Locality& locality() const override {
    return logical_host_->locality();
  }
  Stats::StatName localityZoneStatName() const override {
    return logical_host_->localityZoneStatName();
  }
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    // Should never be called since real hosts are used only for forwarding and not health
    // checking.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  uint32_t priority() const override { return logical_host_->priority(); }
  void priority(uint32_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

private:
  const Network::Address::InstanceConstSharedPtr address_;
  const HostConstSharedPtr logical_host_;
};

/**
 * Pass-through wrapper for cluster info. Derived classes can you use this class as a base if they
 * need to override individual methods.
 */
class ClusterInfoWrapper : public Upstream::ClusterInfo {
public:
  ClusterInfoWrapper(const Upstream::ClusterInfoConstSharedPtr& real_cluster_info)
      : real_cluster_info_(real_cluster_info) {}

  // Upstream::ClusterInfo
  bool addedViaApi() const override { return real_cluster_info_->addedViaApi(); }
  std::chrono::milliseconds connectTimeout() const override {
    return real_cluster_info_->connectTimeout();
  }
  const absl::optional<std::chrono::milliseconds> idleTimeout() const override {
    return real_cluster_info_->idleTimeout();
  }
  uint32_t perConnectionBufferLimitBytes() const override {
    return real_cluster_info_->perConnectionBufferLimitBytes();
  }
  uint64_t features() const override { return real_cluster_info_->features(); }
  const Http::Http2Settings& http2Settings() const override {
    return real_cluster_info_->http2Settings();
  }
  const envoy::api::v2::Cluster::CommonLbConfig& lbConfig() const override {
    return real_cluster_info_->lbConfig();
  }
  Upstream::LoadBalancerType lbType() const override { return real_cluster_info_->lbType(); }
  envoy::api::v2::Cluster::DiscoveryType type() const override {
    return real_cluster_info_->type();
  }
  const absl::optional<envoy::api::v2::Cluster::CustomClusterType>& clusterType() const override {
    return real_cluster_info_->clusterType();
  }
  const absl::optional<envoy::api::v2::Cluster::LeastRequestLbConfig>&
  lbLeastRequestConfig() const override {
    return real_cluster_info_->lbLeastRequestConfig();
  }
  const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>&
  lbRingHashConfig() const override {
    return real_cluster_info_->lbRingHashConfig();
  }
  const absl::optional<envoy::api::v2::Cluster::OriginalDstLbConfig>&
  lbOriginalDstConfig() const override {
    return real_cluster_info_->lbOriginalDstConfig();
  }
  bool maintenanceMode() const override { return real_cluster_info_->maintenanceMode(); }
  uint64_t maxRequestsPerConnection() const override {
    return real_cluster_info_->maxRequestsPerConnection();
  }
  const std::string& name() const override { return real_cluster_info_->name(); }
  Upstream::ResourceManager& resourceManager(Upstream::ResourcePriority priority) const override {
    return real_cluster_info_->resourceManager(priority);
  }
  Network::TransportSocketFactory& transportSocketFactory() const override {
    return real_cluster_info_->transportSocketFactory();
  }
  Upstream::ClusterStats& stats() const override { return real_cluster_info_->stats(); }
  Stats::Scope& statsScope() const override { return real_cluster_info_->statsScope(); }
  Upstream::ClusterLoadReportStats& loadReportStats() const override {
    return real_cluster_info_->loadReportStats();
  }
  const Network::Address::InstanceConstSharedPtr& sourceAddress() const override {
    return real_cluster_info_->sourceAddress();
  }
  const Upstream::LoadBalancerSubsetInfo& lbSubsetInfo() const override {
    return real_cluster_info_->lbSubsetInfo();
  }
  const envoy::api::v2::core::Metadata& metadata() const override {
    return real_cluster_info_->metadata();
  }
  const Envoy::Config::TypedMetadata& typedMetadata() const override {
    return real_cluster_info_->typedMetadata();
  }
  const Network::ConnectionSocket::OptionsSharedPtr& clusterSocketOptions() const override {
    return real_cluster_info_->clusterSocketOptions();
  }
  bool drainConnectionsOnHostRemoval() const override {
    return real_cluster_info_->drainConnectionsOnHostRemoval();
  }
  bool warmHosts() const override { return real_cluster_info_->warmHosts(); }
  absl::optional<std::string> eds_service_name() const override {
    return real_cluster_info_->eds_service_name();
  }
  Upstream::ProtocolOptionsConfigConstSharedPtr
  extensionProtocolOptions(const std::string& name) const override {
    return real_cluster_info_->extensionProtocolOptions(name);
  }

private:
  const Upstream::ClusterInfoConstSharedPtr real_cluster_info_;
};

} // namespace Upstream
} // namespace Envoy

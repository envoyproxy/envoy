#pragma once

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * A logical family of hosts.
 */
class LogicalHostDescription : public HostDescriptionImplBase {
public:
  LogicalHostDescription(
      ClusterInfoConstSharedPtr cluster, const std::string& hostname,
      Network::Address::InstanceConstSharedPtr dest_address, MetadataConstSharedPtr metadata,
      const envoy::config::core::v3::Locality& locality,
      const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig& health_check_config,
      uint32_t priority, TimeSource& time_source)
      : HostDescriptionImplBase(cluster, hostname, dest_address, metadata, locality,
                                health_check_config, priority, time_source),
        address_(dest_address) {}

protected:
  const Network::Address::InstanceConstSharedPtr address_ ABSL_GUARDED_BY(address_lock_);
  mutable absl::Mutex address_lock_;
};

/**
 * A host implementation that can have its address changed in order to create temporal "real"
 * hosts.
 */
class LogicalHost : public HostImplBase, public LogicalHostDescription {
public:
  LogicalHost(
      const ClusterInfoConstSharedPtr& cluster, const std::string& hostname,
      const Network::Address::InstanceConstSharedPtr& address,
      const std::vector<Network::Address::InstanceConstSharedPtr>& address_list,
      const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
      const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint,
      const Network::TransportSocketOptionsConstSharedPtr& override_transport_socket_options,
      TimeSource& time_source)
      : HostImplBase(/*cluster, hostname, address,
                 // TODO(zyfjeff): Created through metadata shared pool
                 std::make_shared<const envoy::config::core::v3::Metadata>(lb_endpoint.metadata()),
                 lb_endpoint.load_balancing_weight().value(), locality_lb_endpoint.locality(),
                 lb_endpoint.endpoint().health_check_config(), locality_lb_endpoint.priority(),
                 lb_endpoint.health_status(), time_source*/
                     lb_endpoint.load_balancing_weight().value(),
                     lb_endpoint.endpoint().health_check_config(), lb_endpoint.health_status()),
        LogicalHostDescription(
            cluster, hostname, address,
            std::make_shared<const envoy::config::core::v3::Metadata>(lb_endpoint.metadata()),
            locality_lb_endpoint.locality(), lb_endpoint.endpoint().health_check_config(),
            locality_lb_endpoint.priority(), time_source),
        override_transport_socket_options_(override_transport_socket_options) {
    setAddressList(address_list);
  }

  // Set the new address. Updates are typically rare so a R/W lock is used for address updates.
  // Note that the health check address update requires no lock to be held since it is only
  // used on the main thread, but we do so anyway since it shouldn't be perf critical and will
  // future proof the code.
  void setNewAddresses(const Network::Address::InstanceConstSharedPtr& address,
                       const std::vector<Network::Address::InstanceConstSharedPtr>& address_list,
                       const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint) {
    const auto& health_check_config = lb_endpoint.endpoint().health_check_config();
    auto health_check_address = resolveHealthCheckAddress(health_check_config, address);
    absl::WriterMutexLock lock(&address_lock_);
    setAddress(address);
    setAddressList(address_list);
    /* TODO: the health checker only gets the first address in the list and
     * will not walk the full happy eyeballs list. We should eventually fix
     * this. */
    setHealthCheckAddress(health_check_address);
  }

  // Upstream::Host
  CreateConnectionData createConnection(
      Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
      Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const override;

  // Upstream::HostDescription
  Network::Address::InstanceConstSharedPtr address() const override {
    absl::ReaderMutexLock lock(&address_lock_);
    return address_; // HostImpl::address();
  }

  const std::pair<Network::Address::InstanceConstSharedPtr,
                  const std::vector<Network::Address::InstanceConstSharedPtr>>
  addressAndListCopy() const {
    absl::ReaderMutexLock lock(&address_lock_);
    return {address_, address_list_};
  }

  void setAddress(Network::Address::InstanceConstSharedPtr address) override { address_ = address; }
  const std::vector<Network::Address::InstanceConstSharedPtr>& addressList() const override {
    absl::ReaderMutexLock lock(&address_lock_);
    return address_list_;
  }
  void setAddressList(
      const std::vector<Network::Address::InstanceConstSharedPtr>& address_list) override {
    absl::WriterMutexLock lock(&address_lock_);
    address_list_ = address_list;
    ASSERT(address_list_.empty() || *address_list_.front() == *address_);
  }

private:
  Network::Address::InstanceConstSharedPtr address_;
  // The first entry in the address_list_ should match the value in address_.
  std::vector<Network::Address::InstanceConstSharedPtr>
      address_list_ ABSL_GUARDED_BY(address_lock_);

  /*  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    absl::ReaderMutexLock lock(&address_lock_);
    return HostImpl::healthCheckAddress();
    }*/

  void setHealthChecker(HealthCheckHostMonitorPtr&& health_checker) override {
    setHealthCheckerImpl(std::move(health_checker));
  }
  void setOutlierDetector(Outlier::DetectorHostMonitorPtr&& outlier_detector) override {
    setOutlierDetectorImpl(std::move(outlier_detector));
  }

  void setLastHcPassTime(MonotonicTime last_hc_pass_time) override {
    setLastHcPassTimeImpl(std::move(last_hc_pass_time));
  }

  const Network::TransportSocketOptionsConstSharedPtr override_transport_socket_options_;
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
  void canary(bool) override {}
  MetadataConstSharedPtr metadata() const override { return logical_host_->metadata(); }
  void metadata(MetadataConstSharedPtr) override {}

  Network::UpstreamTransportSocketFactory& transportSocketFactory() const override {
    return logical_host_->transportSocketFactory();
  }
  const ClusterInfo& cluster() const override { return logical_host_->cluster(); }
  bool canCreateConnection(Upstream::ResourcePriority priority) const override {
    return logical_host_->canCreateConnection(priority);
  }
  HealthCheckHostMonitor& healthChecker() const override { return logical_host_->healthChecker(); }
  Outlier::DetectorHostMonitor& outlierDetector() const override {
    return logical_host_->outlierDetector();
  }
  HostStats& stats() const override { return logical_host_->stats(); }
  LoadMetricStats& loadMetricStats() const override { return logical_host_->loadMetricStats(); }
  const std::string& hostnameForHealthChecks() const override {
    return logical_host_->hostnameForHealthChecks();
  }
  const std::string& hostname() const override { return logical_host_->hostname(); }
  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  // absl::ReaderMutexLock lock(&address_lock_);
  // return logical_host_->address();
  //}
  const std::vector<Network::Address::InstanceConstSharedPtr>& addressList() const override {
    return logical_host_->addressList();
  }
  const envoy::config::core::v3::Locality& locality() const override {
    return logical_host_->locality();
  }
  Stats::StatName localityZoneStatName() const override {
    return logical_host_->localityZoneStatName();
  }
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    // Should never be called since real hosts are used only for forwarding.
    return nullptr;
  }
  absl::optional<MonotonicTime> lastHcPassTime() const override {
    return logical_host_->lastHcPassTime();
  }
  uint32_t priority() const override { return logical_host_->priority(); }
  void priority(uint32_t) override {}

  void setAddress(Network::Address::InstanceConstSharedPtr /*address*/) override {
    ASSERT(false);
    // address_ = address;
    // logical_host_->setAddress(address);
  }

  void setAddressList(
      const std::vector<Network::Address::InstanceConstSharedPtr>& /*address_list*/) override {
    ASSERT(false);
    // logical_host_->setAddressList(address_list);
  }

  Network::UpstreamTransportSocketFactory&
  resolveTransportSocketFactory(const Network::Address::InstanceConstSharedPtr& dest_address,
                                const envoy::config::core::v3::Metadata* metadata) const override {
    return logical_host_->resolveTransportSocketFactory(dest_address, metadata);
  }

private:
  const Network::Address::InstanceConstSharedPtr address_;
  const HostConstSharedPtr logical_host_;
};

} // namespace Upstream
} // namespace Envoy

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

  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    absl::MutexLock lock(&address_lock_);
    return health_check_address_;
  }

protected:
  Network::Address::InstanceConstSharedPtr address_ ABSL_GUARDED_BY(address_lock_);
  Network::Address::InstanceConstSharedPtr health_check_address_ ABSL_GUARDED_BY(address_lock_);
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
      const Network::Address::InstanceConstSharedPtr& address, const AddressVector& address_list,
      const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
      const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint,
      const Network::TransportSocketOptionsConstSharedPtr& override_transport_socket_options,
      TimeSource& time_source)
      : HostImplBase(lb_endpoint.load_balancing_weight().value(),
                     lb_endpoint.endpoint().health_check_config(), lb_endpoint.health_status()),
        LogicalHostDescription(
            cluster, hostname, address,
            // TODO(zyfjeff): Created through metadata shared pool
            std::make_shared<const envoy::config::core::v3::Metadata>(lb_endpoint.metadata()),
            locality_lb_endpoint.locality(), lb_endpoint.endpoint().health_check_config(),
            locality_lb_endpoint.priority(), time_source),
        override_transport_socket_options_(override_transport_socket_options) {
    setAddressList(address_list);
    health_check_address_ =
        resolveHealthCheckAddress(lb_endpoint.endpoint().health_check_config(), address);
  }

  // Set the new address. Updates are typically rare so a R/W lock is used for
  // address updates. Note that the health check address update requires no
  // lock to be held since it is only used on the main thread, but we do so
  // anyway since it shouldn't be perf critical and will future proof the code.
  //
  // TODO: the health checker only gets the first address in the list and will
  // not walk the full happy eyeballs list. We should eventually fix this.
  void setNewAddresses(const Network::Address::InstanceConstSharedPtr& address,
                       const AddressVector& address_list,
                       const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint) {
    const auto& health_check_config = lb_endpoint.endpoint().health_check_config();
    auto health_check_address = resolveHealthCheckAddress(health_check_config, address);
    absl::MutexLock lock(&address_lock_);
    address_ = address;
    setAddressListLockHeld(address_list);
    health_check_address_ = health_check_address;
  }

  /**
   * Sets the address-list: this can be called dynamically during operation, and
   * is thread-safe. Note this is not an override of an interface method but
   * there is a similar method in HostDescriptionImpl which is not thread safe.
   *
   * @param address_list the new address list.
   */
  void setAddressList(const AddressVector& address_list) {
    absl::MutexLock lock(&address_lock_);
    setAddressListLockHeld(address_list);
  }

  void setAddressListLockHeld(const AddressVector& address_list)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(address_lock_) {
    address_list_ = std::make_shared<AddressVector>(address_list);
    ASSERT(address_list_->empty() || *address_list_->front() == *address_);
  }

  SharedAddressVector addressList() const override {
    absl::MutexLock lock(&address_lock_);
    return address_list_;
  }

  // Upstream::Host
  CreateConnectionData createConnection(
      Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
      Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const override;

  // Upstream::HostDescription
  Network::Address::InstanceConstSharedPtr address() const override {
    absl::MutexLock lock(&address_lock_);
    return address_;
  }

  using AddressAndListPair =
      std::pair<Network::Address::InstanceConstSharedPtr, SharedAddressVector>;
  const AddressAndListPair addressAndListCopy() const {
    absl::MutexLock lock(&address_lock_);
    return {address_, address_list_};
  }

private:
  //  The first entry in the address_list_ should match the value in address_.
  SharedAddressVector address_list_ ABSL_GUARDED_BY(address_lock_);

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
  // absl::MutexLock lock(&address_lock_);
  // return logical_host_->address();
  //}
  SharedAddressVector addressList() const override { return logical_host_->addressList(); }
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

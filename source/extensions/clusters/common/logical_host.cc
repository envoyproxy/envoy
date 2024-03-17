#include "source/extensions/clusters/common/logical_host.h"

namespace Envoy {
namespace Upstream {

Network::Address::InstanceConstSharedPtr LogicalHostDescription::healthCheckAddress() const {
  absl::MutexLock lock(&address_lock_);
  return health_check_address_;
}

LogicalHost::LogicalHost(
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
  address_list_ = std::make_shared<AddressVector>(address_list);
  ASSERT(address_list_->empty() || *address_list_->front() == *address_);
  health_check_address_ =
      resolveHealthCheckAddress(lb_endpoint.endpoint().health_check_config(), address);
}

void LogicalHost::setNewAddresses(const Network::Address::InstanceConstSharedPtr& address,
                                  const AddressVector& address_list,
                                  const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint) {
  const auto& health_check_config = lb_endpoint.endpoint().health_check_config();
  auto health_check_address = resolveHealthCheckAddress(health_check_config, address);
  absl::MutexLock lock(&address_lock_);
  address_ = address;
  address_list_ = std::make_shared<AddressVector>(address_list);
  ASSERT(address_list_->empty() || *address_list_->front() == *address_);
  health_check_address_ = health_check_address;
}

HostDescription::SharedConstAddressVector LogicalHost::addressList() const {
  absl::MutexLock lock(&address_lock_);
  return address_list_;
}

Network::Address::InstanceConstSharedPtr LogicalHost::address() const {
  absl::MutexLock lock(&address_lock_);
  return address_;
}

Upstream::Host::CreateConnectionData LogicalHost::createConnection(
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const {
  Network::Address::InstanceConstSharedPtr address;
  SharedConstAddressVector address_list;
  {
    absl::MutexLock lock(&address_lock_);
    address = address_;
    address_list = address_list_;
  }
  return HostImplBase::createConnection(
      dispatcher, cluster(), address, *address_list, transportSocketFactory(), options,
      override_transport_socket_options_ != nullptr ? override_transport_socket_options_
                                                    : transport_socket_options,
      std::make_shared<RealHostDescription>(address, shared_from_this()));
}

} // namespace Upstream
} // namespace Envoy

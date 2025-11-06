#include "source/common/network/happy_eyeballs_connection_impl.h"

#include "envoy/network/address.h"

#include "source/common/network/connection_impl.h"

namespace Envoy {
namespace Network {

HappyEyeballsConnectionProvider::HappyEyeballsConnectionProvider(
    Event::Dispatcher& dispatcher, const std::vector<Address::InstanceConstSharedPtr>& address_list,
    const std::shared_ptr<const Upstream::UpstreamLocalAddressSelector>&
        upstream_local_address_selector,
    UpstreamTransportSocketFactory& socket_factory,
    TransportSocketOptionsConstSharedPtr transport_socket_options,
    const Upstream::HostDescriptionConstSharedPtr& host,
    const ConnectionSocket::OptionsSharedPtr options,
    OptRef<const envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig>
        happy_eyeballs_config)
    : dispatcher_(dispatcher),
      address_list_(sortAddressesWithConfig(address_list, happy_eyeballs_config)),
      upstream_local_address_selector_(upstream_local_address_selector),
      socket_factory_(socket_factory), transport_socket_options_(transport_socket_options),
      host_(host), options_(options) {}

bool HappyEyeballsConnectionProvider::hasNextConnection() {
  return next_address_ < address_list_.size();
}

ClientConnectionPtr HappyEyeballsConnectionProvider::createNextConnection(const uint64_t id) {
  if (first_connection_created_) {
    // The stats for the first connection are handled in ActiveClient::ActiveClient
    host_->stats().cx_total_.inc();
    host_->cluster().trafficStats()->upstream_cx_total_.inc();
  }
  first_connection_created_ = true;
  ASSERT(hasNextConnection());
  ENVOY_LOG_EVENT(debug, "happy_eyeballs_cx_attempt", "C[{}] address={}", id,
                  address_list_[next_address_]->asStringView());
  auto& address = address_list_[next_address_++];
  auto upstream_local_address = upstream_local_address_selector_->getUpstreamLocalAddress(
      address, options_, makeOptRefFromPtr(transport_socket_options_.get()));

  return dispatcher_.createClientConnection(
      address, upstream_local_address.address_,
      socket_factory_.createTransportSocket(transport_socket_options_, host_),
      upstream_local_address.socket_options_, transport_socket_options_);
}

size_t HappyEyeballsConnectionProvider::nextConnection() { return next_address_; }

size_t HappyEyeballsConnectionProvider::totalConnections() { return address_list_.size(); }

namespace {
bool hasMatchingAddressFamily(const Address::InstanceConstSharedPtr& a,
                              const Address::InstanceConstSharedPtr& b) {
  return (a->type() == Address::Type::Ip && b->type() == Address::Type::Ip &&
          a->ip()->version() == b->ip()->version());
}

bool hasMatchingIpVersion(const Address::IpVersion& ip_version,
                          const Address::InstanceConstSharedPtr& addr) {
  return (addr->type() == Address::Type::Ip && addr->ip()->version() == ip_version);
}

} // namespace

std::vector<Address::InstanceConstSharedPtr> HappyEyeballsConnectionProvider::sortAddresses(
    const std::vector<Address::InstanceConstSharedPtr>& in) {
  std::vector<Address::InstanceConstSharedPtr> address_list;
  address_list.reserve(in.size());
  // Iterator which will advance through all addresses matching the first family.
  auto first = in.begin();
  // Iterator which will advance through all addresses not matching the first family.
  // This initial value is ignored and will be overwritten in the loop below.
  auto other = in.begin();
  while (first != in.end() || other != in.end()) {
    if (first != in.end()) {
      address_list.push_back(*first);
      first = std::find_if(first + 1, in.end(),
                           [&](const auto& val) { return hasMatchingAddressFamily(in[0], val); });
    }

    if (other != in.end()) {
      other = std::find_if(other + 1, in.end(),
                           [&](const auto& val) { return !hasMatchingAddressFamily(in[0], val); });

      if (other != in.end()) {
        address_list.push_back(*other);
      }
    }
  }
  ASSERT(address_list.size() == in.size());
  return address_list;
}

std::vector<Address::InstanceConstSharedPtr>
HappyEyeballsConnectionProvider::sortAddressesWithConfig(
    const std::vector<Address::InstanceConstSharedPtr>& in,
    OptRef<const envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig>
        happy_eyeballs_config) {
  if (!happy_eyeballs_config.has_value()) {
    return sortAddresses(in);
  }
  // Sort the addresses according to https://datatracker.ietf.org/doc/html/rfc8305#section-4.
  // Currently the first_address_family version and count options are supported. This allows
  // specifying the address family version to prefer over the other, and the number (count)
  // of addresses in that family to attempt before moving to the other family.
  // If no family version is specified, the version is taken from the first
  // address in the list. The default count is 1. As an example, assume the
  // family version is v6, and the count is 3, then the output list will be:
  // [3*v6, 1*v4, 3*v6, 1*v4, ...] (assuming sufficient addresses exist in the input).
  ENVOY_LOG_EVENT(trace, "happy_eyeballs_sort_address", "sort address with happy_eyeballs config.");
  std::vector<Address::InstanceConstSharedPtr> address_list;
  address_list.reserve(in.size());

  // First_family_ip_version defaults to the first valid ip version
  // unless overwritten by happy_eyeballs_config. There must be at least one
  // entry in the vector that is passed to the function.
  ASSERT(!in.empty());
  Address::IpVersion first_family_ip_version = in[0].get()->ip()->version();

  const auto first_address_family_count =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(*happy_eyeballs_config, first_address_family_count, 1);
  switch (happy_eyeballs_config->first_address_family_version()) {
  case envoy::config::cluster::v3::UpstreamConnectionOptions::DEFAULT:
    break;
  case envoy::config::cluster::v3::UpstreamConnectionOptions::V4:
    first_family_ip_version = Address::IpVersion::v4;
    break;
  case envoy::config::cluster::v3::UpstreamConnectionOptions::V6:
    first_family_ip_version = Address::IpVersion::v6;
    break;
  default:
    break;
  }

  auto first = std::find_if(in.begin(), in.end(), [&](const auto& val) {
    return hasMatchingIpVersion(first_family_ip_version, val);
  });
  auto other = std::find_if(in.begin(), in.end(), [&](const auto& val) {
    return !hasMatchingIpVersion(first_family_ip_version, val);
  });

  while (first != in.end() || other != in.end()) {
    uint32_t count = 0;
    while (first != in.end() && ++count <= first_address_family_count) {
      address_list.push_back(*first);
      first = std::find_if(first + 1, in.end(), [&](const auto& val) {
        return hasMatchingIpVersion(first_family_ip_version, val);
      });
    }

    if (other != in.end()) {
      address_list.push_back(*other);
      other = std::find_if(other + 1, in.end(), [&](const auto& val) {
        return !hasMatchingIpVersion(first_family_ip_version, val);
      });
    }
  }
  ASSERT(address_list.size() == in.size());
  return address_list;
}

} // namespace Network
} // namespace Envoy

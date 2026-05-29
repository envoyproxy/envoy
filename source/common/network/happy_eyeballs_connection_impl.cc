#include "source/common/network/happy_eyeballs_connection_impl.h"

#include <set>

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
    const envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig&
        happy_eyeballs_config)
    : dispatcher_(dispatcher), address_list_(sortAddresses(address_list, happy_eyeballs_config)),
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

struct AddressFamily {
  Address::Type type;
  absl::optional<Address::IpVersion> version;

  bool operator==(const AddressFamily& other) const {
    return type == other.type && version == other.version;
  }

  bool operator<(const AddressFamily& other) const {
    if (type != other.type) {
      return type < other.type;
    }
    return version < other.version;
  }
};

AddressFamily getFamily(const Address::InstanceConstSharedPtr& addr) {
  if (addr->type() == Address::Type::Ip) {
    return {Address::Type::Ip, addr->ip()->version()};
  }
  return {addr->type(), absl::nullopt};
}

} // namespace

std::vector<Address::InstanceConstSharedPtr> HappyEyeballsConnectionProvider::sortAddresses(
    const std::vector<Address::InstanceConstSharedPtr>& in,
    const envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig&
        happy_eyeballs_config) {
  // Sort the addresses according to https://datatracker.ietf.org/doc/html/rfc8305#section-4.
  // Currently the first_address_family version and count options are supported. This allows
  // specifying the address family version to prefer over others, and the number (count) of
  // addresses in that family to attempt before moving to the next family.
  //
  // If no family version is specified, the version is taken from the first address in the list.
  // The default count is 1. As an example, assume the first family version is v6, and the count
  // is 3, then the output list will be:
  //
  //     [3*v6, 1*v4, 3*v6, 1*v4, ...]
  //
  // assuming sufficient addresses exist in the input.
  //
  // This implementation generalizes this to multiple address types (IPv4, IPv6, Pipe, Internal).
  ENVOY_LOG_EVENT(trace, "happy_eyeballs_sort_address", "sort address with happy_eyeballs config.");
  std::vector<Address::InstanceConstSharedPtr> address_list;
  address_list.reserve(in.size());

  ASSERT(!in.empty());

  AddressFamily preferred_family = getFamily(in[0]);
  switch (happy_eyeballs_config.first_address_family_version()) {
  case envoy::config::cluster::v3::UpstreamConnectionOptions::DEFAULT:
    break;
  case envoy::config::cluster::v3::UpstreamConnectionOptions::V4:
    preferred_family = {Address::Type::Ip, Address::IpVersion::v4};
    break;
  case envoy::config::cluster::v3::UpstreamConnectionOptions::V6:
    preferred_family = {Address::Type::Ip, Address::IpVersion::v6};
    break;
  case envoy::config::cluster::v3::UpstreamConnectionOptions::PIPE:
    preferred_family = {Address::Type::Pipe, absl::nullopt};
    break;
  case envoy::config::cluster::v3::UpstreamConnectionOptions::INTERNAL:
    preferred_family = {Address::Type::EnvoyInternal, absl::nullopt};
    break;
  default:
    break;
  }

  // Group addresses by family, preserving original family order except placing preferred_family
  // in the first position. Store each family with an index that will be used for iterating through
  // the bucket of addresses with that address family.
  std::vector<std::pair<AddressFamily, uint32_t>> family_order;
  std::map<AddressFamily, std::vector<Address::InstanceConstSharedPtr>> buckets;
  for (const auto& addr : in) {
    AddressFamily family = getFamily(addr);
    auto& bucket = buckets[family];
    if (bucket.empty()) {
      if (family == preferred_family) {
        family_order.insert(family_order.begin(), {family, 0});
      } else {
        family_order.push_back({family, 0});
      }
    }
    bucket.push_back(addr);
  }

  const auto first_address_family_count =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(happy_eyeballs_config, first_address_family_count, 1);

  // Loop through address families.
  for (int i = 0; address_list.size() < in.size(); i = (i + 1) % family_order.size()) {
    std::vector<Address::InstanceConstSharedPtr>& bucket = buckets[family_order[i].first];
    // Push first_address_family_count addresses for the preferred family. We can't just check if
    // i == 0 because the preferred family may not be present.
    int num_addrs_to_push =
        (family_order[i].first == preferred_family) ? first_address_family_count : 1;
    for (int j = 0; family_order[i].second < bucket.size() && j < num_addrs_to_push; ++j) {
      address_list.push_back(std::move(bucket[family_order[i].second++]));
    }
  }

  ASSERT(address_list.size() == in.size());
  return address_list;
}

} // namespace Network
} // namespace Envoy

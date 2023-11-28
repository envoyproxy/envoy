#include "source/common/network/happy_eyeballs_connection_impl.h"

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
    const ConnectionSocket::OptionsSharedPtr options)
    : dispatcher_(dispatcher), address_list_(sortAddresses(address_list)),
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
  auto upstream_local_address =
      upstream_local_address_selector_->getUpstreamLocalAddress(address, options_);

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

} // namespace Network
} // namespace Envoy

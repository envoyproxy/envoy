#pragma once

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/dns.h"
#include "envoy/network/resolver.h"

#include "common/config/well_known_names.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Implementation of a resolver for SRV records.
 */
class SrvResolver : public Resolver {
public:
  SrvResolver(Network::DnsResolverSharedPtr dns_resolver, Event::Dispatcher& dispatcher)
      : dns_resolver_(dns_resolver), dispatcher_(dispatcher) {}

  // Address::Resolver
  InstanceConstSharedPtr
  resolve(const envoy::api::v2::core::SocketAddress& socket_address) override;
  std::string name() const override { return Config::AddressResolverNames::get().SRV; }

private:
  InstanceConstSharedPtr resolve(std::string socket_address);

  Network::DnsResolverSharedPtr dns_resolver_;
  Event::Dispatcher& dispatcher_;
};

/**
 * Create an Instance from a envoy::api::v2::core::Address.
 * @param address supplies the address proto to resolve.
 * @return pointer to the Instance.
 */
Address::InstanceConstSharedPtr resolveProtoAddress(const envoy::api::v2::core::Address& address);

/**
 * Create an Instance from a envoy::api::v2::core::SocketAddress.
 * @param address supplies the socket address proto to resolve.
 * @return pointer to the Instance.
 */
Address::InstanceConstSharedPtr
resolveProtoSocketAddress(const envoy::api::v2::core::SocketAddress& address);

} // namespace Address
} // namespace Network
} // namespace Envoy

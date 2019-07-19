#include "common/network/resolver_impl.h"

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/address.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "absl/strings/match.h"
#include "absl/synchronization/blocking_counter.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Implementation of a resolver for IP addresses.
 */
class IpResolver : public Resolver {

public:
  InstanceConstSharedPtr
  resolve(const envoy::api::v2::core::SocketAddress& socket_address) override {
    switch (socket_address.port_specifier_case()) {
    case envoy::api::v2::core::SocketAddress::kPortValue:
    // Default to port 0 if no port value is specified.
    case envoy::api::v2::core::SocketAddress::PORT_SPECIFIER_NOT_SET:
      return Network::Utility::parseInternetAddress(
          socket_address.address(), socket_address.port_value(), !socket_address.ipv4_compat());

    default:
      throw EnvoyException(fmt::format("IP resolver can't handle port specifier type {}",
                                       socket_address.port_specifier_case()));
    }
  }

  std::string name() const override { return Config::AddressResolverNames::get().IP; }
};

/**
 * Static registration for the IP resolver. @see RegisterFactory.
 */
static Registry::RegisterFactory<IpResolver, Resolver> ip_registered_;

InstanceConstSharedPtr
SrvResolver::resolve(const envoy::api::v2::core::SocketAddress& socket_address) {
  switch (socket_address.port_specifier_case()) {
  case envoy::api::v2::core::SocketAddress::kNamedPort:
    if (!absl::EqualsIgnoreCase(socket_address.named_port(), "srv")) {
      throw EnvoyException(
          fmt::format("SRV resolver can't handle the named port {}", socket_address.named_port()));
    }
    return resolve(socket_address.address());

  default:
    throw EnvoyException(fmt::format("SRV resolver can't handle port specifier type {}",
                                     socket_address.port_specifier_case()));
  }
}

InstanceConstSharedPtr SrvResolver::resolve(std::string socket_address) {
  bool timed_out;
  InstanceConstSharedPtr address;
  absl::BlockingCounter latch(1);

  Network::ActiveDnsQuery* active_query = dns_resolver_->resolveSrv(
      socket_address, DnsLookupFamily::Auto,
      [&address, &timed_out, &latch](const std::list<Network::DnsResponse>&& srv_records) -> void {
        address = srv_records.front().address_;
        timed_out = false;
        latch.DecrementCount();
      });

  Event::TimerPtr dns_timeout = dispatcher_.createTimer([&timed_out, &latch]() -> void {
    timed_out = true;
    latch.DecrementCount();
  });
  dns_timeout->enableTimer(std::chrono::seconds(5));

  latch.Wait();
  dns_timeout->disableTimer();
  dns_timeout.reset();
  if (active_query) {
    active_query->cancel();
  }

  if (timed_out) {
    throw EnvoyException(fmt::format("SRV resolver timed out for address {}", socket_address));
  }
  return address;
}

InstanceConstSharedPtr resolveProtoAddress(const envoy::api::v2::core::Address& address) {
  switch (address.address_case()) {
  case envoy::api::v2::core::Address::kSocketAddress:
    return resolveProtoSocketAddress(address.socket_address());
  case envoy::api::v2::core::Address::kPipe:
    return InstanceConstSharedPtr{new PipeInstance(address.pipe().path())};
  default:
    throw EnvoyException("Address must be a socket or pipe: " + address.DebugString());
  }
}

InstanceConstSharedPtr
resolveProtoSocketAddress(const envoy::api::v2::core::SocketAddress& socket_address) {
  Resolver* resolver = nullptr;
  const std::string& resolver_name = socket_address.resolver_name();
  if (resolver_name.empty()) {
    resolver =
        Registry::FactoryRegistry<Resolver>::getFactory(Config::AddressResolverNames::get().IP);
  } else {
    resolver = Registry::FactoryRegistry<Resolver>::getFactory(resolver_name);
  }
  if (resolver == nullptr) {
    throw EnvoyException(fmt::format("Unknown address resolver: {}", resolver_name));
  }
  return resolver->resolve(socket_address);
}

} // namespace Address
} // namespace Network
} // namespace Envoy

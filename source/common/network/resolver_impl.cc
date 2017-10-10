#include "envoy/common/exception.h"
#include "envoy/network/address.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Implementation of a resolver for IP addresses
 */
class IpResolver : public Resolver {

public:
  Network::Address::InstanceConstSharedPtr resolve(const std::string& address,
                                                   const uint32_t port) override {
    return Network::Utility::parseInternetAddress(address, port);
  }

  Network::Address::InstanceConstSharedPtr resolve(const std::string&,
                                                   const std::string&) override {
    throw EnvoyException("named ports are not supported by this resolver");
  }
};

/**
 * Implementation of an IP address resolver factory
 */
class IpResolverFactory : public ResolverFactory {
public:
  ResolverPtr create() const override { return ResolverPtr{new IpResolver()}; }

  std::string name() override { return Config::AddressResolverNames::get().IP; }
};

/**
 * Static registration for the IP resolver. @see RegisterFactory.
 */
static Registry::RegisterFactory<IpResolverFactory, ResolverFactory> ip_registered_;

} // namespace Address
} // namespace Network
} // namespace Envoy

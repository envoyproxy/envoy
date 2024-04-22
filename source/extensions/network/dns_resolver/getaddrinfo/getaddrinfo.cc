#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

namespace Envoy {
namespace Network {

// getaddrinfo DNS resolver factory
class GetAddrInfoDnsResolverFactory : public DnsResolverFactory,
                                      public Logger::Loggable<Logger::Id::dns> {
public:
  std::string name() const override { return {"envoy.network.dns_resolver.getaddrinfo"}; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::extensions::network::dns_resolver::getaddrinfo::v3::
                                         GetAddrInfoDnsResolverConfig()};
  }

  DnsResolverSharedPtr
  createDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                    const envoy::config::core::v3::TypedExtensionConfig&) const override {
    return std::make_shared<GetAddrInfoDnsResolver>(dispatcher, api);
  }
};

// Register the CaresDnsResolverFactory
REGISTER_FACTORY(GetAddrInfoDnsResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy

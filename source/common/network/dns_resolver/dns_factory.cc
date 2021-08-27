#include "source/common/network/dns_resolver/dns_factory.h"

namespace Envoy {
namespace Network {

// Create an empty c-ares DNS resolver typed config.
void makeEmptyCaresDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(CaresDnsResolver));
}

// Create an empty apple DNS resolver typed config.
void makeEmptyAppleDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig apple;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(apple);
  typed_dns_resolver_config.set_name(std::string(AppleDnsResolver));
}

// Overloading the template function for DnsFilterConfig type, which doesn't need to copy anything.
void handleLegacyDnsResolverData(
    const envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig::
        ClientContextConfig&,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  makeEmptyCaresDnsResolverConfig(typed_dns_resolver_config);
}

// Overloading the template function for Cluster config type, which need to copy
// both set_use_tcp_for_dns_lookups and dns_resolvers.
void handleLegacyDnsResolverData(
    const envoy::config::cluster::v3::Cluster& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.mutable_dns_resolver_options()->set_use_tcp_for_dns_lookups(
      config.use_tcp_for_dns_lookups());
  if (!config.dns_resolvers().empty()) {
    cares.mutable_resolvers()->MergeFrom(config.dns_resolvers());
  }
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(CaresDnsResolver));
}

} // namespace Network
} // namespace Envoy

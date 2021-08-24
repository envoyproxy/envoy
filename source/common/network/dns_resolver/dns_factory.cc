#include "source/common/network/dns_resolver/dns_factory.h"

namespace Envoy {
namespace Network {

// Create an empty c-ares DNS resolver typed config.
void makeEmptyCaresDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(CaresDnsResolver);
}

// Create an empty apple DNS resolver typed config.
void makeEmptyAppleDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig apple;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(apple);
  typed_dns_resolver_config.set_name(AppleDnsResolver);
}

// Special handling for DnsFilterConfig, which don't need to copy anything over.
template <>
void handleLegacyDnsResolverData<
    envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig::ClientContextConfig>(
    const envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig::
        ClientContextConfig&,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  makeEmptyCaresDnsResolverConfig(typed_dns_resolver_config);
}

// Special handling for Cluster config type, which need to copy both set_use_tcp_for_dns_lookups and
// dns_resolvers.
template <>
void handleLegacyDnsResolverData<envoy::config::cluster::v3::Cluster>(
    const envoy::config::cluster::v3::Cluster& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.mutable_dns_resolver_options()->set_use_tcp_for_dns_lookups(
      config.use_tcp_for_dns_lookups());
  if (!config.dns_resolvers().empty()) {
    cares.mutable_resolvers()->MergeFrom(config.dns_resolvers());
  }
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(CaresDnsResolver);
}

} // namespace Network
} // namespace Envoy

#pragma once

#include "envoy/api/api.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/extensions/filters/udp/dns_filter/v3alpha/dns_filter.pb.h"
#include "envoy/extensions/network/dns_resolver/apple/v3/apple_dns_resolver.pb.h"
#include "envoy/extensions/network/dns_resolver/cares/v3/cares_dns_resolver.pb.h"
#include "envoy/network/dns.h"

#include "source/common/config/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Network {

const std::string CaresDnsResolver = "envoy.network.dns_resolver.cares";
const std::string AppleDnsResolver = "envoy.network.dns_resolver.apple";
const std::string DnsResolverCategory = "envoy.network.dns_resolver";

class DnsResolverFactory : public Config::TypedFactory {
public:
  /**
   * @returns a DnsResolver object.
   * @param dispatcher: the local dispatcher thread
   * @param api: API interface to interact with system resources
   * @param typed_dns_resolver_config: the typed DNS resolver config
   */
  virtual DnsResolverSharedPtr createDnsResolverImpl(
      Event::Dispatcher& dispatcher, Api::Api& api,
      const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) PURE;

  std::string category() const override { return DnsResolverCategory; }
};

// Create an empty c-ares DNS resolver typed config.
inline void makeEmptyCaresDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(CaresDnsResolver);
}

// Create an empty apple DNS resolver typed config.
inline void makeEmptyAppleDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig apple;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(apple);
  typed_dns_resolver_config.set_name(AppleDnsResolver);
}

// Retrieve the DNS related configurations in the passed in @param config, and store the data into
// @param typed_dns_resolver_config. The design behavior is:
//
// 1) If the config has typed_dns_resolver_config, and the corresponding DNS resolver factory
//    is registered, copy it into typed_dns_resolver_config and use it.
//
// 2) Otherwise, fall back to the default behavior. i.e, check whether this is MacOS, and the
//    run time flag: envoy.restart_features.use_apple_api_for_dns_lookups is enable. If it is,
//    synthetic a AppleDnsResolverConfig object and pack it into typed_dns_resolver_config.

// 3) If it is not MacOS, synthetic a CaresDnsResolverConfig object and pack it into
// typed_dns_resolver_config.
//    This can enable Envoy to use c-ares DNS library during DNS resolving process. The details are:
// 3.1) if dns_resolution_config exists, copy it into CaresDnsResolverConfig,
//      and pack CaresDnsResolverConfig into typed_dns_resolver_config.
// 3.2) if dns_resolution_config doesn't exists, follow below behavior for backward compatibility:
// 3.3) if config is DnsFilterConfig, pack an empty CaresDnsResolverConfig into
//      typed_dns_resolver_config.
// 3.4) For all others, copy config.use_tcp_for_dns_lookups into
//      CaresDnsResolverConfig.dns_resolver_options.use_tcp_for_dns_lookups
// 3.5) For ClusterConfig, one extra thing is to copy dns_resolvers into
//      CaresDnsResolverConfig.resolvers,
// 3.6) Then pack CaresDnsResolverConfig into typed_dns_resolver_config.

template <class T>
void makeDnsResolverConfig(
    const T& config, envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  // typed_dns_resolver_config takes precedence
  if (config.has_typed_dns_resolver_config()) {
    Network::DnsResolverFactory* dns_resolver_factory =
        Config::Utility::getAndCheckFactory<Network::DnsResolverFactory>(
            config.typed_dns_resolver_config(), true);
    if ((dns_resolver_factory != nullptr) &&
        (dns_resolver_factory->category() == DnsResolverCategory)) {
      typed_dns_resolver_config.MergeFrom(config.typed_dns_resolver_config());
      return;
    }
  }

  // Checking MacOS
  if (Runtime::runtimeFeatureEnabled("envoy.restart_features.use_apple_api_for_dns_lookups") &&
      Config::Utility::getAndCheckFactoryByName<Network::DnsResolverFactory>(AppleDnsResolver,
                                                                             true)) {
    makeEmptyAppleDnsResolverConfig(typed_dns_resolver_config);
    return;
  }
  // Fall back to default behavior.
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  if (config.has_dns_resolution_config()) {
    // Copy resolvers if config has it.
    if (!config.dns_resolution_config().resolvers().empty()) {
      cares.mutable_resolvers()->MergeFrom(config.dns_resolution_config().resolvers());
    }
    cares.mutable_dns_resolver_options()->MergeFrom(
        config.dns_resolution_config().dns_resolver_options());
  } else {
    // Skipping copying these fields for DnsFilterConfig.
    if constexpr (!(std::is_same_v<T, envoy::extensions::filters::udp::dns_filter::v3alpha::
                                          DnsFilterConfig::ClientContextConfig>)) {
      cares.mutable_dns_resolver_options()->set_use_tcp_for_dns_lookups(
          config.use_tcp_for_dns_lookups());
      if constexpr (std::is_same_v<T, envoy::config::cluster::v3::Cluster>) {
        if (!config.dns_resolvers().empty()) {
          cares.mutable_resolvers()->MergeFrom(
              // for cluster config, need to copy dns_resolvers field if not empty.
              config.dns_resolvers());
        }
      }
    }
  }
  // Pack CaresDnsResolverConfig object into typed_dns_resolver_config.
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(CaresDnsResolver);
}

} // namespace Network
} // namespace Envoy

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

const std::string cares_dns_resolver = "envoy.dns_resolver.cares";
const std::string apple_dns_resolver = "envoy.dns_resolver.apple";
const std::string dns_resolver_category = "envoy.network_dnsresolvers";

class DnsResolverFactory : public Config::TypedFactory {
public:
  /**
   * @returns a callback to create a DnsResolver.
   */

  virtual DnsResolverSharedPtr createDnsResolverCb(
      Event::Dispatcher& dispatcher, Api::Api& api,
      const envoy::config::core::v3::TypedExtensionConfig& dns_resolver_config) PURE;

  std::string category() const override { return dns_resolver_category; }
};

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
//    This can enable Envoy to use cares DNS library during DNS resolving process. The details are:
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
    Network::DnsResolverFactory* dns_resolver_factory;
    dns_resolver_factory = Config::Utility::getAndCheckFactory<Network::DnsResolverFactory>(
        config.typed_dns_resolver_config(), true);
    if ((dns_resolver_factory != nullptr) &&
        (dns_resolver_factory->category() == dns_resolver_category)) {
      typed_dns_resolver_config.MergeFrom(config.typed_dns_resolver_config());
      return;
    }
  }

  // Checking MacOS
  if (Config::Utility::getAndCheckFactoryByName<Network::DnsResolverFactory>(apple_dns_resolver,
                                                                             true)) {
    static bool use_apple_api_for_dns_lookups =
        Runtime::runtimeFeatureEnabled("envoy.restart_features.use_apple_api_for_dns_lookups");
    if (use_apple_api_for_dns_lookups) {
      envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig apple;
      // Pack AppleDnsResolverConfig object into typed_dns_resolver_config.
      typed_dns_resolver_config.mutable_typed_config()->PackFrom(apple);
      typed_dns_resolver_config.set_name(apple_dns_resolver);
      return;
    }
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
  typed_dns_resolver_config.set_name(cares_dns_resolver);
}

} // namespace Network
} // namespace Envoy

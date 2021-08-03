#pragma once

#include "envoy/api/api.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/extensions/filters/udp/dns_filter/v3alpha/dns_filter.pb.h"
#include "envoy/extensions/network/dns_resolver/apple/v3/appl_dns_resolver.pb.h"
#include "envoy/extensions/network/dns_resolver/cares/v3/cares_dns_resolver.pb.h"
#include "envoy/network/dns.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Network {

class DnsResolverFactory : public Config::TypedFactory {
public:
  /**
   * @returns a callback to create a DnsResolver.
   */

  virtual DnsResolverSharedPtr createDnsResolverCb(
      Event::Dispatcher& dispatcher, const Api::Api& api,
      const envoy::config::core::v3::TypedExtensionConfig& dns_resolver_config) PURE;

  std::string category() const override { return "envoy.network_dnsresolvers"; }
};

// Retrieve the DNS related configurations in the passed in @param config, and store the data into
// @param typed_dns_resolver_config. The design behavior is:
//
// 1) If the config has typed_dns_resolver_config, copy it into typed_dns_resolver_config and use
// it.
//
// 2) Otherwise, synthetic a CaresDnsResolverConfig object for Envoy to use cares DNS library.
// 2.1) if dns_resolution_config exists, copy it into CaresDnsResolverConfig,
//      and pack CaresDnsResolverConfig into typed_dns_resolver_config.
// 2.2) if dns_resolution_config doesn't exists, follow below behavior for backward compatibility:
// 2.3) if config is DnsFilterConfig, pack an empty CaresDnsResolverConfig into
// typed_dns_resolver_config. 2.4) For all others, copy config.use_tcp_for_dns_lookups into
//      CaresDnsResolverConfig.dns_resolver_options.use_tcp_for_dns_lookups
// 2.5) For ClusterConfig, one extra thing is to copy dns_resolvers into
// CaresDnsResolverConfig.resolvers, 2.6) Then pack CaresDnsResolverConfig into
// typed_dns_resolver_config. Note, to make cares DNS library to work, In file:
// source/extensions/extensions_build_config.bzl, cares DNS extension need to be enabled:
//      "envoy.dns_resolver.cares": "//source/extensions/network/dns_resolver/cares:dns_lib",

// For Envoy running on Apple system, to use apple DNS library as DNS resolver, the following need
// to be done: 3.1) In file: source/extensions/extensions_build_config.bzl, enable Apple DNS
// extension by adding below line
//      "envoy.dns_resolver.apple": "//source/extensions/network/dns_resolver/apple:apple_dns_lib",
// 3.2) In running configuration file, add apple DNS as a typed extension in
// *typed_dns_resolver_config* field. For example, to add such configuration in bootstrap (or other
// configurations like cluster, dynamic_forward_proxy):
//    :ref:'typed_dns_resolver_config
//    <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.typed_dns_resolver_config>'
// Here is the configuration:
//      typed_dns_resolver_config:
//        name: envoy.dns_resolver.apple
//        typed_config:
//          "@type":
//          type.googleapis.com/envoy.extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig

template <class T>
static inline void
makeDnsResolverConfig(const T& config,
                      envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {

  if (config.has_typed_dns_resolver_config()) {
    typed_dns_resolver_config.MergeFrom(config.typed_dns_resolver_config());
  } else {
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
    if (config.has_dns_resolution_config()) {
      cares.mutable_resolvers()->MergeFrom(config.dns_resolution_config().resolvers());
      cares.mutable_dns_resolver_options()->MergeFrom(
          config.dns_resolution_config().dns_resolver_options());
    } else {
      // Skipping copying these fields for DnsFilterConfig.
      if constexpr (!(std::is_same_v<T, envoy::extensions::filters::udp::dns_filter::v3alpha::
                                            DnsFilterConfig::ClientContextConfig>)) {
        cares.mutable_dns_resolver_options()->set_use_tcp_for_dns_lookups(
            config.use_tcp_for_dns_lookups());
        if constexpr (std::is_same_v<T, envoy::config::cluster::v3::Cluster>) {
          cares.mutable_resolvers()->MergeFrom(
              config.dns_resolvers()); // for cluster config, need to copy dns_resolvers field.
        }
      }
    }
    // Pack CaresDnsResolverConfig object into typed_dns_resolver_config.
    typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
    typed_dns_resolver_config.set_name("envoy.dns_resolver.cares");
  }
}

} // namespace Network
} // namespace Envoy

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

constexpr absl::string_view CaresDnsResolver = "envoy.network.dns_resolver.cares";
constexpr absl::string_view AppleDnsResolver = "envoy.network.dns_resolver.apple";
constexpr absl::string_view DnsResolverCategory = "envoy.network.dns_resolver";

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

  std::string category() const override { return std::string(DnsResolverCategory); }
};

// Create an empty c-ares DNS resolver typed config.
void makeEmptyCaresDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// Create an empty apple DNS resolver typed config.
void makeEmptyAppleDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// If the config has typed_dns_resolver_config, and the corresponding DNS resolver factory is
// registered, copy it into typed_dns_resolver_config and return true. Otherwise, return false.
template <class ConfigType>
bool checkTypedDnsResolverConfigExist(
    const ConfigType& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  if (config.has_typed_dns_resolver_config()) {
    Network::DnsResolverFactory* dns_resolver_factory =
        Config::Utility::getAndCheckFactory<Network::DnsResolverFactory>(
            config.typed_dns_resolver_config(), true);
    if ((dns_resolver_factory != nullptr) &&
        (dns_resolver_factory->category() == std::string(DnsResolverCategory))) {
      typed_dns_resolver_config.MergeFrom(config.typed_dns_resolver_config());
      return true;
    }
  }
  return false;
}

// If the config has dns_resolution_config, synthetic a CaresDnsResolverConfig typed config based on
// it.
template <class ConfigType>
bool checkDnsResolutionConfigExist(
    const ConfigType& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  if (config.has_dns_resolution_config()) {
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
    if (!config.dns_resolution_config().resolvers().empty()) {
      cares.mutable_resolvers()->MergeFrom(config.dns_resolution_config().resolvers());
    }
    cares.mutable_dns_resolver_options()->MergeFrom(
        config.dns_resolution_config().dns_resolver_options());
    typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
    typed_dns_resolver_config.set_name(std::string(CaresDnsResolver));
    return true;
  }
  return false;
}

// For backward compatibility, copy over set_use_tcp_for_dns_lookups from config, and synthetic
// a CaresDnsResolverConfig typed config. This logic fit for bootstrap, and dns_cache config types.
template <class ConfigType>
void handleLegacyDnsResolverData(
    const ConfigType& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.mutable_dns_resolver_options()->set_use_tcp_for_dns_lookups(
      config.use_tcp_for_dns_lookups());
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(CaresDnsResolver));
}

// Special handling for DnsFilterConfig, which don't need to copy anything over.
template <>
void handleLegacyDnsResolverData<
    envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig::ClientContextConfig>(
    const envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig::
        ClientContextConfig&,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// Special handling for Cluster config type, which need to copy both set_use_tcp_for_dns_lookups and
// dns_resolvers.
template <>
void handleLegacyDnsResolverData<envoy::config::cluster::v3::Cluster>(
    const envoy::config::cluster::v3::Cluster& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// Retrieve the DNS related configurations in the passed in @param config, and store the data into
// @param typed_dns_resolver_config.
template <class ConfigType>
void makeDnsResolverConfig(
    const ConfigType& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  // typed_dns_resolver_config takes precedence
  if (checkTypedDnsResolverConfigExist(config, typed_dns_resolver_config)) {
    return;
  }

  // If it is MacOS and the run time flag: envoy.restart_features.use_apple_api_for_dns_lookups
  // is enabled, synthetic an AppleDnsResolverConfig typed config.
  if ((Config::Utility::getAndCheckFactoryByName<Network::DnsResolverFactory>(std::string(AppleDnsResolver),
                                                                              true) != nullptr) &&
      Runtime::runtimeFeatureEnabled("envoy.restart_features.use_apple_api_for_dns_lookups")) {
    makeEmptyAppleDnsResolverConfig(typed_dns_resolver_config);
    return;
  }

  // If dns_resolution_config exits, create a CaresDnsResolverConfig typed config based on it.
  if (checkDnsResolutionConfigExist(config, typed_dns_resolver_config)) {
    return;
  }

  // Handle legacy DNS resolver fields for backward compatibility. Different config type has
  // different fields to copy.
  handleLegacyDnsResolverData(config, typed_dns_resolver_config);
}

} // namespace Network
} // namespace Envoy

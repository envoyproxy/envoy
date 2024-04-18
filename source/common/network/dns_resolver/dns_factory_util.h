#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.h"
#include "envoy/extensions/network/dns_resolver/apple/v3/apple_dns_resolver.pb.h"
#include "envoy/extensions/network/dns_resolver/cares/v3/cares_dns_resolver.pb.h"
#include "envoy/network/dns_resolver.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Network {

// Create a default c-ares DNS resolver typed config.
void makeDefaultCaresDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// Create a default apple DNS resolver typed config.
void makeDefaultAppleDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// Create a default DNS resolver typed config based on build system and configuration.
void makeDefaultDnsResolverConfig(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// If it is MacOS and it's compiled it, create an AppleDnsResolverConfig typed config.
bool tryUseAppleApiForDnsLookups(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// If the config has typed_dns_resolver_config, copy it over.
template <class ConfigType>
bool checkTypedDnsResolverConfigExist(
    const ConfigType& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  if (config.has_typed_dns_resolver_config()) {
    typed_dns_resolver_config.MergeFrom(config.typed_dns_resolver_config());
    return true;
  }
  return false;
}

// If the config has dns_resolution_config, create a CaresDnsResolverConfig typed config based on
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

// For backward compatibility, copy over use_tcp_for_dns_lookups from config, and create
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

// Overloading the template function for DnsFilterConfig type, which doesn't need to copy anything.
void handleLegacyDnsResolverData(
    const envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig::ClientContextConfig&,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// Overloading the template function for Cluster config type, which need to copy
// both use_tcp_for_dns_lookups and dns_resolvers.
void handleLegacyDnsResolverData(
    const envoy::config::cluster::v3::Cluster& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// Make typed_dns_resolver_config from the passed @param config.
template <class ConfigType>
envoy::config::core::v3::TypedExtensionConfig makeDnsResolverConfig(const ConfigType& config) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;

  // typed_dns_resolver_config takes precedence
  if (checkTypedDnsResolverConfigExist(config, typed_dns_resolver_config)) {
    return typed_dns_resolver_config;
  }

  // If use apple API for DNS lookups, create an AppleDnsResolverConfig typed config.
  if (tryUseAppleApiForDnsLookups(typed_dns_resolver_config)) {
    return typed_dns_resolver_config;
  }

  // If dns_resolution_config exits, create a CaresDnsResolverConfig typed config based on it.
  if (checkDnsResolutionConfigExist(config, typed_dns_resolver_config)) {
    return typed_dns_resolver_config;
  }

  // Handle legacy DNS resolver fields for backward compatibility.
  // Different config type has different fields to copy.
  handleLegacyDnsResolverData(config, typed_dns_resolver_config);
  return typed_dns_resolver_config;
}

// Create the DNS resolver factory from the typed config. This is the underline
// function which performs the registry lookup based on typed config.
Network::DnsResolverFactory& createDnsResolverFactoryFromTypedConfig(
    const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// Create the default DNS resolver factory. apple for MacOS or c-ares for all others.
// The default registry lookup will always succeed, thus no exception throwing.
// This function can be called in main or worker threads.
Network::DnsResolverFactory& createDefaultDnsResolverFactory(
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

// Create the DNS resolver factory from the proto config.
// The passed in config parameter may contain invalid typed_dns_resolver_config.
// In that case, the underline registry lookup will throw an exception.
// This function has to be called in main thread.
template <class ConfigType>
Network::DnsResolverFactory& createDnsResolverFactoryFromProto(
    const ConfigType& config,
    envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  typed_dns_resolver_config = makeDnsResolverConfig(config);
  return createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
}

} // namespace Network
} // namespace Envoy

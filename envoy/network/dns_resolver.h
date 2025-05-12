#pragma once

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"

#include "source/common/config/utility.h"

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
   * @param typed_dns_resolver_config: the typed DNS resolver config or error status.
   */
  virtual absl::StatusOr<DnsResolverSharedPtr> createDnsResolver(
      Event::Dispatcher& dispatcher, Api::Api& api,
      const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) const PURE;

  std::string category() const override { return std::string(DnsResolverCategory); }

  /**
   * Initialize the related data for this type of DNS resolver.
   * For some DNS resolvers, like c-ares, there are some specific data structure
   * needs to be initialized before using it to resolve target.
   */
  virtual void initialize() {}

  /**
   * Cleanup the related data for this type of DNS resolver.
   * For some DNS resolvers, like c-ares, there are some specific data structure
   * needs to be cleaned up before terminates Envoy.
   */
  virtual void terminate() {}

  /**
   * Create the DNS resolver factory based on the typed config and initialize it.
   * @returns the DNS Resolver factory.
   * @param typed_dns_resolver_config: the typed DNS resolver config
   */
  static Network::DnsResolverFactory&
  createFactory(const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

  /**
   * Call the terminate method on all the registered DNS resolver factories.
   */
  static void terminateFactories();
};

} // namespace Network
} // namespace Envoy

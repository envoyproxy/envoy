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
  /*
   * @returns a DnsResolver object.
   * @param dispatcher: the local dispatcher thread
   * @param api: API interface to interact with system resources
   * @param typed_dns_resolver_config: the typed DNS resolver config
   */
  virtual DnsResolverSharedPtr createDnsResolver(
      Event::Dispatcher& dispatcher, Api::Api& api,
      const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) const PURE;

  std::string category() const override { return std::string(DnsResolverCategory); }
};

} // namespace Network
} // namespace Envoy

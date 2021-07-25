#pragma once

#include "envoy/network/dns.h"
#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Network {


class DnsResolverFactory : public Config::TypedFactory {
public:
  /**
   * @returns a callback to create a DnsResolver.
   */

  virtual DnsResolverSharedPtr  createDnsResolverCb(Event::Dispatcher& dispatcher,
                                                    const Api::Api& api,
                                                    const envoy::config::core::v3::TypedExtensionConfig& dns_resolver_config) PURE;

  std::string category() const override { return "envoy.network_dnsresolvers"; }
};


} // namespace Network
} // namespace Envoy

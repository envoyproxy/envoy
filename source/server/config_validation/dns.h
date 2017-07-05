#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"

namespace Envoy {
namespace Network {

/**
 * DnsResolver to be used in config validation runs. Every DNS query immediately fails to resolve,
 * since we never need DNS information to validate a config. (If a config contains an unresolveable
 * name, it still passes validation -- for example, we might be running validation in a test
 * environment, while the name resolves fine in prod.)
 */
class ValidationDnsResolver : public DnsResolver {
public:
  // Network::DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override;
};

} // namespace Network
} // namespace Envoy

#pragma once

#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Network {

/**
 * An active async DNS query.
 */
class ActiveDnsQuery {
public:
  virtual ~ActiveDnsQuery() = default;

  /**
   * Cancel an outstanding DNS request.
   */
  virtual void cancel() PURE;
};

/**
 * DNS response.
 */
struct DnsResponse {
  DnsResponse(const Address::InstanceConstSharedPtr& address, const std::chrono::seconds ttl)
      : address_(address), ttl_(ttl) {}

  const Address::InstanceConstSharedPtr address_;
  const std::chrono::seconds ttl_;
};

enum class DnsLookupFamily { V4Only, V6Only, Auto };

/**
 * An asynchronous DNS resolver.
 */
class DnsResolver {
public:
  virtual ~DnsResolver() = default;

  /**
   * Called when a resolution attempt is complete.
   * @param response supplies the list of resolved IP addresses and TTLs. The list will be empty if
   *                     the resolution failed.
   */
  using ResolveCb = std::function<void(std::list<DnsResponse>&& response)>;

  /**
   * Initiate an async DNS resolution.
   * @param dns_name supplies the DNS name to lookup.
   * @param dns_lookup_family the DNS IP version lookup policy.
   * @param callback supplies the callback to invoke when the resolution is complete.
   * @return if non-null, a handle that can be used to cancel the resolution.
   *         This is only valid until the invocation of callback or ~DnsResolver().
   */
  virtual ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                                  ResolveCb callback) PURE;
};

using DnsResolverSharedPtr = std::shared_ptr<DnsResolver>;

} // namespace Network
} // namespace Envoy

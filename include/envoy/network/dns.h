#pragma once

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

namespace Network {

/**
 * An active async DNS query.
 */
class ActiveDnsQuery {
public:
  virtual ~ActiveDnsQuery() {}

  /**
   * Cancel an outstanding DNS request.
   */
  virtual void cancel() PURE;
};

/**
 * An asynchronous DNS resolver.
 */
class DnsResolver {
public:
  virtual ~DnsResolver() {}

  /**
   * Called when a resolution attempt is complete.
   * @param address_list supplies the list of resolved IP addresses. The list will be empty if
   *                     the resolution failed.
   */
  typedef std::function<void(std::list<Address::InstancePtr>&& address_list)> ResolveCb;

  /**
   * Initiate an async DNS resolution.
   * @param dns_name supplies the DNS name to lookup.
   * @param callback supplies the callback to invoke when the resolution is complete.
   * @return a handle that can be used to cancel the resolution.
   */
  virtual ActiveDnsQuery& resolve(const std::string& dns_name, ResolveCb callback) PURE;
};

typedef std::unique_ptr<DnsResolver> DnsResolverPtr;

} // Network

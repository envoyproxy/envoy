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

  enum class CancelReason {
    // The caller no longer needs the answer to the query.
    QueryAbandoned,
    // The query timed out from the perspective of the caller. The DNS implementation may take
    // a different action in this case (e.g., destroying existing DNS connections) in an effort
    // to get an answer to future queries.
    Timeout
  };

  /**
   * Cancel an outstanding DNS request.
   * @param reason supplies the cancel reason.
   */
  virtual void cancel(CancelReason reason) PURE;
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

/**
 * DNS SRV record response.
 */
struct DnsSrvResponse {
  DnsSrvResponse(const std::string& host, uint16_t port, uint16_t priority, uint16_t weight)
      : host_(host), port_(port), priority_(priority), weight_(weight) {}

  const std::string host_;
  const uint16_t port_;
  const uint16_t priority_;
  const uint16_t weight_;
};

enum class DnsLookupFamily { V4Only, V6Only, Auto, V4Preferred, All };

enum class DnsResourceType : uint8_t { SRV = 33 };

/**
 * An asynchronous DNS resolver.
 */
class DnsResolver {
public:
  virtual ~DnsResolver() = default;

  /**
   * Final status for a DNS resolution.
   */
  enum class ResolutionStatus { Success, Failure };

  /**
   * Called when a resolution attempt is complete.
   * @param status supplies the final status of the resolution.
   * @param response supplies the list of resolved IP addresses and TTLs.
   */
  using ResolveCb = std::function<void(ResolutionStatus status, std::list<DnsResponse>&& response)>;

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

  /**
   * Initiate an async DNS query. This function is used for querying specific type of DNS record.
   * Currently, it supports only SRV record resolution. When looking up A/AAAA record,
   * we should use DnsResolver::resolve.
   * @param dns_name supplies the DNS name to lookup.
   * @param resource_type the DNS resource type.
   * @return if non-null, a handle that can be used to cancel the resolution.
   *         This is only valid until the invocation of callback or ~DnsResolver().
   */
  virtual ActiveDnsQuery* query(const std::string& dns_name, DnsResourceType resource_type) PURE;
};

using DnsResolverSharedPtr = std::shared_ptr<DnsResolver>;

} // namespace Network
} // namespace Envoy

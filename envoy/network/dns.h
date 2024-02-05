#pragma once

#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

#include "absl/types/variant.h"

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
 * DNS A/AAAA record response.
 */
struct AddrInfoResponse {
  const Address::InstanceConstSharedPtr address_;
  const std::chrono::seconds ttl_;
};

/**
 * DNS SRV record response.
 */
struct SrvResponse {
  const std::string host_;
  const uint16_t port_;
  const uint16_t priority_;
  const uint16_t weight_;
};

enum class RecordType { A, AAAA, SRV };

enum class DnsLookupFamily { V4Only, V6Only, Auto, V4Preferred, All };

class DnsResponse {
public:
  DnsResponse(const Address::InstanceConstSharedPtr& address, const std::chrono::seconds ttl)
      : response_(AddrInfoResponse{
            address,
            std::chrono::seconds(std::min(std::chrono::seconds::rep(INT_MAX),
                                          std::max(ttl.count(), std::chrono::seconds::rep(0))))}) {}
  DnsResponse(const std::string& host, uint16_t port, uint16_t priority, uint16_t weight)
      : response_(SrvResponse{host, port, priority, weight}) {}

  const AddrInfoResponse& addrInfo() const { return absl::get<AddrInfoResponse>(response_); }

  const SrvResponse& srv() const { return absl::get<SrvResponse>(response_); }

private:
  absl::variant<AddrInfoResponse, SrvResponse> response_;
};

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
   * Tell the resolver to reset networking, typically in response to a network switch (e.g., from
   * WiFi to cellular). What the resolver does is resolver dependent but might involve creating
   * new resolver connections, re-reading resolver targets, etc.
   */
  virtual void resetNetworking() PURE;
};

using DnsResolverSharedPtr = std::shared_ptr<DnsResolver>;

} // namespace Network
} // namespace Envoy

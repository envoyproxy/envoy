#pragma once

#include <dns_sd.h>

#include <cstdint>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/registry/registry.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Network {

// This abstraction allows for finer control in tests by using a mocked API. Production code simply
// forwards the function calls to Apple's API.
class DnsService {
public:
  virtual ~DnsService() = default;
  virtual void dnsServiceRefDeallocate(DNSServiceRef sdRef);
  virtual DNSServiceErrorType dnsServiceCreateConnection(DNSServiceRef* sdRef);
  virtual dnssd_sock_t dnsServiceRefSockFD(DNSServiceRef sdRef);
  virtual DNSServiceErrorType dnsServiceProcessResult(DNSServiceRef sdRef);
  virtual DNSServiceErrorType
  dnsServiceGetAddrInfo(DNSServiceRef* sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                        DNSServiceProtocol protocol, const char* hostname,
                        DNSServiceGetAddrInfoReply callBack, void* context);
};

using DnsServiceSingleton = ThreadSafeSingleton<DnsService>;

/**
 * All DNS resolver stats. @see stats_macros.h
 */
#define ALL_APPLE_DNS_RESOLVER_STATS(COUNTER)                                                      \
  COUNTER(connection_failure)                                                                      \
  COUNTER(get_addr_failure)                                                                        \
  COUNTER(network_failure)                                                                         \
  COUNTER(processing_failure)                                                                      \
  COUNTER(socket_failure)                                                                          \
  COUNTER(timeout)

/**
 * Struct definition for all DNS resolver stats. @see stats_macros.h
 */
struct AppleDnsResolverStats {
  ALL_APPLE_DNS_RESOLVER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Implementation of DnsResolver that uses Apple dns_sd.h APIs. All calls and callbacks are assumed
 * to happen on the thread that owns the creating dispatcher.
 */
class AppleDnsResolverImpl : public DnsResolver, protected Logger::Loggable<Logger::Id::dns> {
public:
  AppleDnsResolverImpl(Event::Dispatcher& dispatcher, Stats::Scope& root_scope);

  static AppleDnsResolverStats generateAppleDnsResolverStats(Stats::Scope& scope);

  // Network::DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override;

private:
  struct PendingResolution;

  // The newly created pending resolution and whether this action was successful. Note
  // that {nullptr, true} is possible in the case where the resolution succeeds inline.
  using StartResolutionResult = std::pair<std::unique_ptr<PendingResolution>, bool>;
  StartResolutionResult startResolution(const std::string& dns_name,
                                        DnsLookupFamily dns_lookup_family, ResolveCb callback);

  void chargeGetAddrInfoErrorStats(DNSServiceErrorType error_code);

  struct PendingResolution : public ActiveDnsQuery {
    PendingResolution(AppleDnsResolverImpl& parent, ResolveCb callback,
                      Event::Dispatcher& dispatcher, const std::string& dns_name,
                      DnsLookupFamily dns_lookup_family);

    ~PendingResolution();

    // Network::ActiveDnsQuery
    void cancel(Network::ActiveDnsQuery::CancelReason reason) override;

    static DnsResponse buildDnsResponse(const struct sockaddr* address, uint32_t ttl);

    void onEventCallback(uint32_t events);
    void finishResolve();

    // Wrappers for the API calls.
    DNSServiceErrorType dnsServiceGetAddrInfo();
    void onDNSServiceGetAddrInfoReply(DNSServiceFlags flags, uint32_t interface_index,
                                      DNSServiceErrorType error_code, const char* hostname,
                                      const struct sockaddr* address, uint32_t ttl);
    bool dnsServiceRefSockFD();

    std::list<DnsResponse>& finalAddressList();

    // Small wrapping struct to accumulate addresses from firings of the
    // onDNSServiceGetAddrInfoReply callback.
    struct PendingResponse {
      ResolutionStatus status_;
      std::list<DnsResponse> v4_responses_;
      std::list<DnsResponse> v6_responses_;
      std::list<DnsResponse> all_responses_;
    };

    AppleDnsResolverImpl& parent_;
    // Caller supplied callback to invoke on query completion or error.
    const ResolveCb callback_;
    // Dispatcher to post any callback_ exceptions to.
    Event::Dispatcher& dispatcher_;
    Event::FileEventPtr sd_ref_event_;
    DNSServiceRef sd_ref_{};
    const std::string dns_name_;
    bool synchronously_completed_{};
    bool owned_{};
    // DNSServiceGetAddrInfo fires one callback DNSServiceGetAddrInfoReply callback per IP address,
    // and informs via flags if more IP addresses are incoming. Therefore, these addresses need to
    // be accumulated before firing callback_.
    PendingResponse pending_response_;
    DnsLookupFamily dns_lookup_family_;
  };

  Event::Dispatcher& dispatcher_;
  Event::TimerPtr initialize_failure_timer_;
  BackOffStrategyPtr backoff_strategy_;
  Stats::ScopePtr scope_;
  AppleDnsResolverStats stats_;
};

DECLARE_FACTORY(AppleDnsResolverFactory);

} // namespace Network
} // namespace Envoy

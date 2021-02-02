#pragma once

#include <dns_sd.h>

#include <cstdint>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"

#include "common/common/backoff_strategy.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/singleton/threadsafe_singleton.h"

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
  COUNTER(socket_failure)                                                                          \
  COUNTER(processing_failure)

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
class AppleDnsResolverImpl : public DnsResolver, protected Logger::Loggable<Logger::Id::upstream> {
public:
  AppleDnsResolverImpl(Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                       Stats::Scope& root_scope);
  ~AppleDnsResolverImpl() override;
  static AppleDnsResolverStats generateAppleDnsResolverStats(Stats::Scope& scope);

  // Network::DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override;

private:
  struct PendingResolution : public ActiveDnsQuery {
    PendingResolution(AppleDnsResolverImpl& parent, ResolveCb callback,
                      Event::Dispatcher& dispatcher, DNSServiceRef sd_ref,
                      const std::string& dns_name)
        : parent_(parent), callback_(callback), dispatcher_(dispatcher),
          /* (taken and edited from dns_sd.h):
           * For efficiency, clients that perform many concurrent operations may want to use a
           * single Unix Domain Socket connection with the background daemon, instead of having a
           * separate connection for each independent operation. To use this mode, clients first
           * call DNSServiceCreateConnection(&SharedRef) to initialize the main DNSServiceRef.
           * For each subsequent operation that is to share that same connection, the client copies
           * the SharedRef, and then passes the address of that copy, setting the ShareConnection
           * flag to tell the library that this DNSServiceRef is not a typical uninitialized
           * DNSServiceRef; it's a copy of an existing DNSServiceRef whose connection information
           * should be reused.
           */
          individual_sd_ref_(sd_ref), dns_name_(dns_name) {}
    ~PendingResolution();

    // Network::ActiveDnsQuery
    void cancel() override;

    static DnsResponse buildDnsResponse(const struct sockaddr* address, uint32_t ttl);
    // Wrapper for the API call.
    DNSServiceErrorType dnsServiceGetAddrInfo(DnsLookupFamily dns_lookup_family);
    // Wrapper for the API callback.
    void onDNSServiceGetAddrInfoReply(DNSServiceFlags flags, uint32_t interface_index,
                                      DNSServiceErrorType error_code, const char* hostname,
                                      const struct sockaddr* address, uint32_t ttl);

    // Small wrapping struct to accumulate addresses from firings of the
    // onDNSServiceGetAddrInfoReply callback.
    struct FinalResponse {
      ResolutionStatus status_;
      std::list<DnsResponse> responses_;
    };

    AppleDnsResolverImpl& parent_;
    // Caller supplied callback to invoke on query completion or error.
    const ResolveCb callback_;
    // Dispatcher to post any callback_ exceptions to.
    Event::Dispatcher& dispatcher_;
    DNSServiceRef individual_sd_ref_;
    const std::string dns_name_;
    bool synchronously_completed_{};
    bool owned_{};
    // DNSServiceGetAddrInfo fires one callback DNSServiceGetAddrInfoReply callback per IP address,
    // and informs via flags if more IP addresses are incoming. Therefore, these addresses need to
    // be accumulated before firing callback_.
    absl::optional<FinalResponse> pending_cb_{};
  };

  void initializeMainSdRef();
  void deallocateMainSdRef();
  void onEventCallback(uint32_t events);
  void addPendingQuery(PendingResolution* query);
  void removePendingQuery(PendingResolution* query);
  void flushPendingQueries(const bool with_error);

  Event::Dispatcher& dispatcher_;
  Event::TimerPtr initialize_failure_timer_;
  BackOffStrategyPtr backoff_strategy_;
  DNSServiceRef main_sd_ref_{nullptr};
  Event::FileEventPtr sd_ref_event_;
  Stats::ScopePtr scope_;
  AppleDnsResolverStats stats_;
  // When using a shared sd ref via DNSServiceCreateConnection, the DNSServiceGetAddrInfoReply
  // callback with the kDNSServiceFlagsMoreComing flag might refer to addresses for various
  // PendingResolutions. Therefore, the resolver needs to have a container of queries pending
  // calling their own callback_s until a DNSServiceGetAddrInfoReply is called with
  // kDNSServiceFlagsMoreComing not set or an error status is received in
  // DNSServiceGetAddrInfoReply.
  std::set<PendingResolution*> queries_with_pending_cb_;
};

} // namespace Network
} // namespace Envoy

#pragma once

#include <dns_sd.h>

#include <cstdint>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/dns.h"

#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/common/utility.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Network {

/**
 * Implementation of DnsResolver that uses Apple dns_sd.h APIs. All calls and callbacks are assumed
 * to happen on the thread that owns the creating dispatcher.
 */
class AppleDnsResolverImpl : public DnsResolver, protected Logger::Loggable<Logger::Id::upstream> {
public:
  AppleDnsResolverImpl(Event::Dispatcher& dispatcher);
  ~AppleDnsResolverImpl() override;

  // Network::DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override;

private:
  struct PendingResolution : public ActiveDnsQuery {
    PendingResolution(AppleDnsResolverImpl& parent, ResolveCb callback,
                      Event::Dispatcher& dispatcher, DNSServiceRef sd_ref,
                      const std::string& dns_name)
        : parent_(parent), callback_(callback), dispatcher_(dispatcher), individual_sd_ref_(sd_ref),
          dns_name_(dns_name) {}
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
  void addPendingQuery(PendingResolution* query) { queries_with_pending_cb_.push_back(query); }
  void flushPendingQueries();

  Event::Dispatcher& dispatcher_;
  DNSServiceRef main_sd_ref_;
  Event::FileEventPtr sd_ref_event_;
  // When using a shared sd ref via DNSServiceCreateConnection, the DNSServiceGetAddrInfoReply
  // callback with the kDNSServiceFlagsMoreComing flag might refer to addresses for various
  // PendingResolutions. Therefore, the resolver needs to have a container of queries pending
  // calling their own callback_s until a DNSServiceGetAddrInfoReply is called with
  // kDNSServiceFlagsMoreComing not set.
  std::list<PendingResolution*> queries_with_pending_cb_;
};

} // namespace Network
} // namespace Envoy

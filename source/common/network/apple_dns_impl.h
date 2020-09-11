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
  friend class AppleDnsResolverImplPeer;
  struct PendingResolution : public ActiveDnsQuery {
    // Network::ActiveDnsQuery
    PendingResolution(AppleDnsResolverImpl& parent, ResolveCb callback,
                      Event::Dispatcher& dispatcher, DNSServiceRef sd_ref,
                      const std::string& dns_name)
        : parent_(parent), callback_(callback), dispatcher_(dispatcher), individual_sd_ref_(sd_ref),
          dns_name_(dns_name) {}

    void cancel() override {
      // dns_sd.h only supports service-wide cancellation, so we just allow the
      // network events to continue but don't invoke the callback on completion.
      cancelled_ = true;
    }

    void onDNSServiceGetAddrInfoReply(DNSServiceFlags flags, uint32_t interface_index,
                                      DNSServiceErrorType error_code, const char* hostname,
                                      const struct sockaddr* address, uint32_t ttl);
    // wrapper for the API call
    DNSServiceErrorType dnsServiceGetAddrInfo(DnsLookupFamily dns_lookup_family);

    AppleDnsResolverImpl& parent_;
    // Caller supplied callback to invoke on query completion or error.
    const ResolveCb callback_;
    // Dispatcher to post any callback_ exceptions to.
    Event::Dispatcher& dispatcher_;
    // Has the query completed?
    bool completed_ = false;
    // Was the query cancelled via cancel()?
    bool cancelled_ = false;
    // If dns_lookup_family is "fallback", fallback to v4 address if v6
    // resolution failed.
    bool fallback_if_failed_ = false;
    DNSServiceRef individual_sd_ref_;
    const std::string dns_name_;
  };

  void initializeMainSdRef();
  void onEventCallback(uint32_t events);

  Event::Dispatcher& dispatcher_;
  DNSServiceRef main_sd_ref_;
  bool dirty_sd_ref_{};
  Event::FileEventPtr sd_ref_event_;
};

} // namespace Network
} // namespace Envoy

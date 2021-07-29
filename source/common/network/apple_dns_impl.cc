#include "source/common/network/apple_dns_impl.h"

#include <dns_sd.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/event/file_event.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Network {

void DnsService::dnsServiceRefDeallocate(DNSServiceRef sdRef) { DNSServiceRefDeallocate(sdRef); }

DNSServiceErrorType DnsService::dnsServiceCreateConnection(DNSServiceRef* sdRef) {
  return DNSServiceCreateConnection(sdRef);
}

dnssd_sock_t DnsService::dnsServiceRefSockFD(DNSServiceRef sdRef) {
  return DNSServiceRefSockFD(sdRef);
}

DNSServiceErrorType DnsService::dnsServiceProcessResult(DNSServiceRef sdRef) {
  return DNSServiceProcessResult(sdRef);
}

DNSServiceErrorType DnsService::dnsServiceGetAddrInfo(DNSServiceRef* sdRef, DNSServiceFlags flags,
                                                      uint32_t interfaceIndex,
                                                      DNSServiceProtocol protocol,
                                                      const char* hostname,
                                                      DNSServiceGetAddrInfoReply callBack,
                                                      void* context) {
  return DNSServiceGetAddrInfo(sdRef, flags, interfaceIndex, protocol, hostname, callBack, context);
}

AppleDnsResolverImpl::AppleDnsResolverImpl(Event::Dispatcher& dispatcher, Stats::Scope& root_scope)
    : dispatcher_(dispatcher), scope_(root_scope.createScope("dns.apple.")),
      stats_(generateAppleDnsResolverStats(*scope_)) {}

AppleDnsResolverStats AppleDnsResolverImpl::generateAppleDnsResolverStats(Stats::Scope& scope) {
  return {ALL_APPLE_DNS_RESOLVER_STATS(POOL_COUNTER(scope))};
}

ActiveDnsQuery* AppleDnsResolverImpl::resolve(const std::string& dns_name,
                                              DnsLookupFamily dns_lookup_family,
                                              ResolveCb callback) {
  ENVOY_LOG(debug, "DNS resolver resolve={}", dns_name);

  Address::InstanceConstSharedPtr address{};
  TRY_ASSERT_MAIN_THREAD {
    // When an IP address is submitted to c-ares in DnsResolverImpl, c-ares synchronously returns
    // the IP without submitting a DNS query. Because Envoy has come to rely on this behavior, this
    // resolver implements a similar resolution path to avoid making improper DNS queries for
    // resolved IPs.
    address = Utility::parseInternetAddress(dns_name);
    ENVOY_LOG(debug, "DNS resolver resolved ({}) to ({}) without issuing call to Apple API",
              dns_name, address->asString());
  }
  END_TRY
  catch (const EnvoyException& e) {
    // Resolution via Apple APIs
    ENVOY_LOG(trace, "DNS resolver local resolution failed with: {}", e.what());

    auto pending_resolution =
        std::make_unique<PendingResolution>(*this, callback, dispatcher_, dns_name);

    DNSServiceErrorType error = pending_resolution->dnsServiceGetAddrInfo(dns_lookup_family);
    if (error != kDNSServiceErr_NoError) {
      ENVOY_LOG(warn, "DNS resolver error ({}) in dnsServiceGetAddrInfo for {}", error, dns_name);
      chargeGetAddrInfoErrorStats(error);
      return nullptr;
    }

    // If the query was synchronously resolved in the Apple API call, there is no need to return the
    // query.
    if (pending_resolution->synchronously_completed_) {
      return nullptr;
    }

    // Otherwise, hook up the query's UDS socket to the event loop to process updates.
    if (!pending_resolution->dnsServiceRefSockFD()) {
      ENVOY_LOG(warn, "DNS resolver error in dnsServiceRefSockFD for {}", dns_name);
      return nullptr;
    }

    pending_resolution->owned_ = true;
    return pending_resolution.release();
  }

  ASSERT(address != nullptr);
  // Finish local, synchronous resolution. This needs to happen outside of the exception block above
  // as the callback itself can throw.
  callback(DnsResolver::ResolutionStatus::Success,
           {DnsResponse(address, std::chrono::seconds(60))});
  return nullptr;
}

void AppleDnsResolverImpl::chargeGetAddrInfoErrorStats(DNSServiceErrorType error_code) {
  switch (error_code) {
  case kDNSServiceErr_DefunctConnection:
    stats_.connection_failure_.inc();
    break;
  case kDNSServiceErr_NoRouter:
    stats_.network_failure_.inc();
    break;
  case kDNSServiceErr_Timeout:
    stats_.timeout_.inc();
    break;
  default:
    stats_.get_addr_failure_.inc();
    break;
  }
}

AppleDnsResolverImpl::PendingResolution::PendingResolution(AppleDnsResolverImpl& parent,
                                                           ResolveCb callback,
                                                           Event::Dispatcher& dispatcher,
                                                           const std::string& dns_name)
    : parent_(parent), callback_(callback), dispatcher_(dispatcher), dns_name_(dns_name),
      pending_cb_({ResolutionStatus::Success, {}}) {}

AppleDnsResolverImpl::PendingResolution::~PendingResolution() {
  ENVOY_LOG(debug, "Destroying PendingResolution for {}", dns_name_);

  // dns_sd.h says:
  //   If the reference's underlying socket is used in a run loop or select() call, it should
  //   be removed BEFORE DNSServiceRefDeallocate() is called, as this function closes the
  //   reference's socket.
  sd_ref_event_.reset();

  // It is possible that DNSServiceGetAddrInfo returns a synchronous error, with a NULLed
  // DNSServiceRef, in AppleDnsResolverImpl::resolve.
  // Additionally, it is also possible that the query is cancelled before resolution starts, and
  // thus the DNSServiceRef is null.
  // Therefore, only deallocate if the ref is not null.
  if (sd_ref_) {
    ENVOY_LOG(debug, "DNSServiceRefDeallocate individual sd ref");
    DnsServiceSingleton::get().dnsServiceRefDeallocate(sd_ref_);
  }
}

void AppleDnsResolverImpl::PendingResolution::cancel(Network::ActiveDnsQuery::CancelReason) {
  // TODO(mattklein123): If cancel reason is timeout, do something more aggressive about destroying
  // and recreating the DNS system to maximize the chance of success in following queries.
  ENVOY_LOG(debug, "Cancelling PendingResolution for {}", dns_name_);
  ASSERT(owned_);
  // Because the query is self-owned, delete now.
  delete this;
}

void AppleDnsResolverImpl::PendingResolution::onEventCallback(uint32_t events) {
  ENVOY_LOG(debug, "DNS resolver file event ({})", events);
  RELEASE_ASSERT(events & Event::FileReadyType::Read,
                 fmt::format("invalid FileReadyType event={}", events));
  DNSServiceErrorType error = DnsServiceSingleton::get().dnsServiceProcessResult(sd_ref_);
  if (error != kDNSServiceErr_NoError) {
    ENVOY_LOG(warn, "DNS resolver error ({}) in DNSServiceProcessResult", error);
    parent_.stats_.processing_failure_.inc();
    // Similar to receiving an error in onDNSServiceGetAddrInfoReply, an error while processing fd
    // events indicates that the sd_ref state is broken.
    // Therefore, finish resolving with an error.
    pending_cb_.status_ = ResolutionStatus::Failure;
    finishResolve();
  }
}

void AppleDnsResolverImpl::PendingResolution::finishResolve() {
  callback_(pending_cb_.status_, std::move(pending_cb_.responses_));

  if (owned_) {
    ENVOY_LOG(debug, "Resolution for {} completed (async)", dns_name_);
    delete this;
  } else {
    ENVOY_LOG(debug, "Resolution for {} completed (synchronously)", dns_name_);
    synchronously_completed_ = true;
  }
}

DNSServiceErrorType
AppleDnsResolverImpl::PendingResolution::dnsServiceGetAddrInfo(DnsLookupFamily dns_lookup_family) {
  DNSServiceProtocol protocol;
  switch (dns_lookup_family) {
  case DnsLookupFamily::V4Only:
    protocol = kDNSServiceProtocol_IPv4;
    break;
  case DnsLookupFamily::V6Only:
    protocol = kDNSServiceProtocol_IPv6;
    break;
  case DnsLookupFamily::Auto:
    protocol = kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6;
    break;
  }

  // TODO: explore caching: there are caching flags in the dns_sd.h flags, allow expired answers
  // from the cache?
  // TODO: explore validation via `DNSSEC`?
  return DnsServiceSingleton::get().dnsServiceGetAddrInfo(
      &sd_ref_, kDNSServiceFlagsTimeout, 0, protocol, dns_name_.c_str(),
      /*
       * About Thread Safety (taken from inline documentation there):
       * The dns_sd.h API does not presuppose any particular threading model, and consequently
       * does no locking internally (which would require linking with a specific threading library).
       * If the client concurrently, from multiple threads (or contexts), calls API routines using
       * the same DNSServiceRef, it is the client's responsibility to provide mutual exclusion for
       * that DNSServiceRef.
       */

      // Therefore, much like the c-ares implementation All calls and callbacks to the API need to
      // happen on the thread that owns the creating dispatcher. This is the case as callbacks are
      // driven by processing bytes in onEventCallback which run on the passed in dispatcher's event
      // loop.
      [](DNSServiceRef, DNSServiceFlags flags, uint32_t interface_index,
         DNSServiceErrorType error_code, const char* hostname, const struct sockaddr* address,
         uint32_t ttl, void* context) {
        static_cast<PendingResolution*>(context)->onDNSServiceGetAddrInfoReply(
            flags, interface_index, error_code, hostname, address, ttl);
      },
      this);
}

void AppleDnsResolverImpl::PendingResolution::onDNSServiceGetAddrInfoReply(
    DNSServiceFlags flags, uint32_t interface_index, DNSServiceErrorType error_code,
    const char* hostname, const struct sockaddr* address, uint32_t ttl) {
  ENVOY_LOG(debug,
            "DNS for {} resolved with: flags={}[MoreComing={}, Add={}], interface_index={}, "
            "error_code={}, hostname={}",
            dns_name_, flags, flags & kDNSServiceFlagsMoreComing ? "yes" : "no",
            flags & kDNSServiceFlagsAdd ? "yes" : "no", interface_index, error_code, hostname);

  // Generic error handling.
  if (error_code != kDNSServiceErr_NoError) {
    parent_.chargeGetAddrInfoErrorStats(error_code);

    pending_cb_.status_ = ResolutionStatus::Failure;
    pending_cb_.responses_.clear();

    finishResolve();
    // Note: Nothing can follow this call to flushPendingQueries due to deletion of this
    // object upon resolution.
    return;
  }

  // dns_sd.h does not call out behavior where callbacks to DNSServiceGetAddrInfoReply
  // would respond without the flag. However, Envoy's API is solely additive.
  // Therefore, only add this address to the list if kDNSServiceFlagsAdd is set.
  if (flags & kDNSServiceFlagsAdd) {
    ASSERT(address, "invalid to add null address");
    auto dns_response = buildDnsResponse(address, ttl);
    ENVOY_LOG(debug, "Address to add address={}, ttl={}",
              dns_response.address_->ip()->addressAsString(), ttl);
    pending_cb_.responses_.push_back(dns_response);
  }

  if (!(flags & kDNSServiceFlagsMoreComing)) {
    ENVOY_LOG(debug, "DNS Resolver flushing queries pending callback");
    finishResolve();
    // Note: Nothing can follow this call to finishResolve due to deletion of this
    // object upon resolution.
    return;
  }
}

bool AppleDnsResolverImpl::PendingResolution::dnsServiceRefSockFD() {
  auto fd = DnsServiceSingleton::get().dnsServiceRefSockFD(sd_ref_);
  // According to dns_sd.h: DnsServiceRefSockFD returns "The DNSServiceRef's underlying socket
  // descriptor, or -1 on error.". Although it gives no detailed description on when/why this call
  // would fail.
  if (fd == -1) {
    parent_.stats_.socket_failure_.inc();
    return false;
  }

  sd_ref_event_ = dispatcher_.createFileEvent(
      fd,
      // note: Event::FileTriggerType::Level is used here to closely resemble the c-ares
      // implementation in dns_impl.cc.
      [this](uint32_t events) { onEventCallback(events); }, Event::FileTriggerType::Level,
      Event::FileReadyType::Read);
  sd_ref_event_->setEnabled(Event::FileReadyType::Read);
  return true;
}

DnsResponse
AppleDnsResolverImpl::PendingResolution::buildDnsResponse(const struct sockaddr* address,
                                                          uint32_t ttl) {
  switch (address->sa_family) {
  case AF_INET:
    sockaddr_in address_in;
    memset(&address_in, 0, sizeof(address_in));
    address_in.sin_family = AF_INET;
    address_in.sin_port = 0;
    address_in.sin_addr = reinterpret_cast<const sockaddr_in*>(address)->sin_addr;
    return {std::make_shared<const Address::Ipv4Instance>(&address_in), std::chrono::seconds(ttl)};
  case AF_INET6:
    sockaddr_in6 address_in6;
    memset(&address_in6, 0, sizeof(address_in6));
    address_in6.sin6_family = AF_INET6;
    address_in6.sin6_port = 0;
    address_in6.sin6_addr = reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr;
    return {std::make_shared<const Address::Ipv6Instance>(address_in6), std::chrono::seconds(ttl)};
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Network
} // namespace Envoy

#include "source/extensions/network/dns_resolver/apple/apple_dns_impl.h"

#include <dns_sd.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/event/file_event.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"
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

AppleDnsResolverImpl::AppleDnsResolverImpl(
    const envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig& proto_config,
    Event::Dispatcher& dispatcher, Stats::Scope& root_scope)
    : dispatcher_(dispatcher), scope_(root_scope.createScope("dns.apple.")),
      stats_(generateAppleDnsResolverStats(*scope_)),
      include_unroutable_families_(proto_config.include_unroutable_families()) {}

AppleDnsResolverStats AppleDnsResolverImpl::generateAppleDnsResolverStats(Stats::Scope& scope) {
  return {ALL_APPLE_DNS_RESOLVER_STATS(POOL_COUNTER(scope))};
}

AppleDnsResolverImpl::StartResolutionResult
AppleDnsResolverImpl::startResolution(const std::string& dns_name,
                                      DnsLookupFamily dns_lookup_family, ResolveCb callback) {
  ENVOY_LOG_EVENT(debug, "apple_dns_start", "DNS resolution for {} started", dns_name);

  // When an IP address is submitted to c-ares in DnsResolverImpl, c-ares synchronously returns
  // the IP without submitting a DNS query. Because Envoy has come to rely on this behavior, this
  // resolver implements a similar resolution path to avoid making improper DNS queries for
  // resolved IPs.
  auto address = Utility::parseInternetAddressNoThrow(dns_name);

  if (address != nullptr) {
    ENVOY_LOG_EVENT(debug, "apple_dns_immediate_resolution",
                    "DNS resolver resolved ({}) to ({}) without issuing call to Apple API",
                    dns_name, address->asString());
    callback(DnsResolver::ResolutionStatus::Completed, "apple_dns_immediate_success",
             {DnsResponse(address, std::chrono::seconds(60))});
    return {nullptr, true};
  }

  ENVOY_LOG(trace, "Performing DNS resolution via Apple APIs");
  auto pending_resolution = std::make_unique<PendingResolution>(*this, callback, dispatcher_,
                                                                dns_name, dns_lookup_family);

  DNSServiceErrorType error =
      pending_resolution->dnsServiceGetAddrInfo(include_unroutable_families_);
  if (error != kDNSServiceErr_NoError) {
    ENVOY_LOG(warn, "DNS resolver error ({}) in dnsServiceGetAddrInfo for {}", error, dns_name);
    chargeGetAddrInfoErrorStats(error);
    return {nullptr, false};
  }

  if (pending_resolution->synchronously_completed_) {
    return {nullptr, true};
  }

  // Hook up the query's UDS socket to the event loop to process updates.
  if (!pending_resolution->dnsServiceRefSockFD()) {
    ENVOY_LOG(warn, "DNS resolver error in dnsServiceRefSockFD for {}", dns_name);
    return {nullptr, false};
  }

  // Return the active resolution query, giving it ownership over itself so that it can
  // can clean itself up once it's done.
  pending_resolution->owned_ = true;

  return {std::move(pending_resolution), true};
}

ActiveDnsQuery* AppleDnsResolverImpl::resolve(const std::string& dns_name,
                                              DnsLookupFamily dns_lookup_family,
                                              ResolveCb callback) {
  auto pending_resolution_and_success = startResolution(dns_name, dns_lookup_family, callback);

  // If we synchronously failed the resolution, trigger a failure callback.
  if (!pending_resolution_and_success.second) {
    ENVOY_LOG_EVENT(debug, "apple_dns_immediate_failure", "DNS resolution for {} failed", dns_name);

    callback(DnsResolver::ResolutionStatus::Failure, "apple_dns_immediate_failure", {});
    return nullptr;
  }

  return pending_resolution_and_success.first.release();
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
                                                           const std::string& dns_name,
                                                           DnsLookupFamily dns_lookup_family)
    : parent_(parent), callback_(callback), dispatcher_(dispatcher), dns_name_(dns_name),
      pending_response_(PendingResponse()), dns_lookup_family_(dns_lookup_family) {}

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

void AppleDnsResolverImpl::PendingResolution::cancel(Network::ActiveDnsQuery::CancelReason reason) {
  ENVOY_LOG_EVENT(debug, "apple_dns_resolution_cancelled",
                  "dns resolution cancelled for {} with reason={}", dns_name_,
                  static_cast<int>(reason));
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
    pending_response_.status_ = ResolutionStatus::Failure;
    pending_response_.details_ = absl::StrCat("apple_dns_error_", error);
    finishResolve();
  }
}

std::list<DnsResponse>& AppleDnsResolverImpl::PendingResolution::finalAddressList() {
  switch (dns_lookup_family_) {
  case DnsLookupFamily::V4Only:
    return pending_response_.v4_responses_;
  case DnsLookupFamily::V6Only:
    return pending_response_.v6_responses_;
  case DnsLookupFamily::Auto:
    // Per API docs only give v4 if v6 is not available.
    if (pending_response_.v6_responses_.empty()) {
      return pending_response_.v4_responses_;
    }
    return pending_response_.v6_responses_;
  case DnsLookupFamily::V4Preferred:
    // Per API docs only give v6 if v4 is not available.
    if (pending_response_.v4_responses_.empty()) {
      return pending_response_.v6_responses_;
    }
    return pending_response_.v4_responses_;
  case DnsLookupFamily::All:
    ASSERT(pending_response_.all_responses_.empty());
    pending_response_.all_responses_.insert(pending_response_.all_responses_.end(),
                                            pending_response_.v4_responses_.begin(),
                                            pending_response_.v4_responses_.end());
    if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.prefer_ipv6_dns_on_macos")) {
      pending_response_.all_responses_.insert(pending_response_.all_responses_.end(),
                                              pending_response_.v6_responses_.begin(),
                                              pending_response_.v6_responses_.end());
    } else {
      pending_response_.all_responses_.insert(pending_response_.all_responses_.begin(),
                                              pending_response_.v6_responses_.begin(),
                                              pending_response_.v6_responses_.end());
    }
    return pending_response_.all_responses_;
  }
  IS_ENVOY_BUG("unexpected DnsLookupFamily enum");
  return pending_response_.all_responses_;
}

void AppleDnsResolverImpl::PendingResolution::finishResolve() {
  ENVOY_LOG_EVENT(debug, "apple_dns_resolution_complete",
                  "dns resolution for {} completed with status {}", dns_name_,
                  static_cast<int>(pending_response_.status_));
  callback_(pending_response_.status_, std::move(pending_response_.details_),
            std::move(finalAddressList()));

  if (owned_) {
    ENVOY_LOG(debug, "Resolution for {} completed (async)", dns_name_);
    delete this;
  } else {
    ENVOY_LOG(debug, "Resolution for {} completed (synchronously)", dns_name_);
    synchronously_completed_ = true;
  }
}

DNSServiceErrorType
AppleDnsResolverImpl::PendingResolution::dnsServiceGetAddrInfo(bool include_unroutable_families) {
  switch (dns_lookup_family_) {
  case DnsLookupFamily::V4Only:
    query_protocol_ = kDNSServiceProtocol_IPv4;
    break;
  case DnsLookupFamily::V6Only:
    query_protocol_ = kDNSServiceProtocol_IPv6;
    break;
  case DnsLookupFamily::Auto:
  case DnsLookupFamily::V4Preferred:
  case DnsLookupFamily::All:
    if (include_unroutable_families) {
      query_protocol_ = kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6;
      break;
    }

    /* We want to make sure we don't get any address that is not routable. Passing 0
     * to apple's `DNSServiceGetAddrInfo` will make a best attempt to filter out IPv6
     * or IPv4 addresses depending on what's routable, per Apple's documentation:
     *
     * If neither flag is set, the system will apply an intelligent heuristic, which
     * is (currently) that it will attempt to look up both, except:
     * If "hostname" is a wide-area unicast DNS hostname (i.e. not a ".local." name) but
     * this host has no routable IPv6 address, then the call will not try to look up IPv6
     * addresses for "hostname", since any addresses it found would be unlikely to be of
     * any use anyway. Similarly, if this host has no routable IPv4 address, the call will
     * not try to look up IPv4 addresses for "hostname".
     */
    query_protocol_ = 0;
    break;
  }

  // TODO: explore caching: there are caching flags in the dns_sd.h flags, allow expired answers
  // from the cache?
  // TODO: explore validation via `DNSSEC`?
  return DnsServiceSingleton::get().dnsServiceGetAddrInfo(
      &sd_ref_, kDNSServiceFlagsTimeout | kDNSServiceFlagsReturnIntermediates, 0, query_protocol_,
      dns_name_.c_str(),
      /*
       * About Thread Safety (taken from inline documentation there):
       * The dns_sd.h API does not presuppose any particular threading model, and consequently
       * does no locking internally (which would require linking with a specific threading library).
       * If the client concurrently, from multiple threads (or contexts), calls API routines using
       * the same DNSServiceRef, it is the client's responsibility to provide mutual exclusion for
       * that DNSServiceRef.
       */

      // Therefore, much like the c-ares implementation, all calls and callbacks to the API need to
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
  // If the DNS query protocol is (kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6) or if it is
  // 0, then this callback is expected to be called at least two times: at least once for IPv4 and
  // at least once for IPv6. This is true even if there are no DNS records for the given address
  // family and/or the network that the code is running on doesn't support the given address family.
  //
  // That means if the network doesn't support an address family or the hostname doesn't have any
  // DNS records for the address family, there will still be at least one callback to
  // onDNSServiceGetAddrInfoReply() for requested address family. In such a case, the `address` will
  // still be non-null and its `sa_family` will be the address family of the query (even if the
  // address itself isn't a meaningful IP address).

  ENVOY_LOG(debug,
            "DNS for {} resolved with: flags={}[MoreComing={}, Add={}], interface_index={}, "
            "error_code={}, hostname={}",
            dns_name_, flags, flags & kDNSServiceFlagsMoreComing ? "yes" : "no",
            flags & kDNSServiceFlagsAdd ? "yes" : "no", interface_index, error_code, hostname);

  // Make sure that we trigger the failure callback if we get an error back.
  // NoSuchRecord is *not* considered an error; it indicates that a query was successfully
  // completed, but there were no DNS records for that address family.
  //
  // If the protocol is set to 0 or set to (kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6),
  // the behavior is undefined in the API docs as to whether there would be more than one callback
  // with an error. However, when we receive an error, we call finishResolve(), which results in
  // the deletion of this PendingResolution instance, and the destructor ensures the DNSServiceRef
  // gets deallocated (via the dnsServiceRefDeallocate() method), which owns the callback
  // operation. Hence, after calling finishResolve(), we are guaranteed to not get any more
  // callbacks to this method.
  if (error_code != kDNSServiceErr_NoError && error_code != kDNSServiceErr_NoSuchRecord) {
    parent_.chargeGetAddrInfoErrorStats(error_code);

    pending_response_.status_ = ResolutionStatus::Failure;
    pending_response_.details_ = absl::StrCat("apple_dns_error_", error_code);
    pending_response_.v4_responses_.clear();
    pending_response_.v6_responses_.clear();

    finishResolve();
    // Note: Nothing can follow this call to flushPendingQueries due to deletion of this
    // object upon resolution.
    return;
  }

  ASSERT(address, "address cannot be null");
  if (address->sa_family == AF_INET) {
    pending_response_.v4_response_received_ = true;
  } else if (address->sa_family == AF_INET6) {
    pending_response_.v6_response_received_ = true;
  }

  // dns_sd.h does not call out behavior where callbacks to DNSServiceGetAddrInfoReply
  // would respond without the flag. However, Envoy's API is solely additive.
  // Therefore, only add this address to the list if kDNSServiceFlagsAdd is set.
  if (error_code == kDNSServiceErr_NoError && (flags & kDNSServiceFlagsAdd)) {
    auto dns_response = buildDnsResponse(address, ttl);
    ENVOY_LOG(debug, "Address to add address={}, ttl={}",
              dns_response.addrInfo().address_->ip()->addressAsString(), ttl);
    if (dns_response.addrInfo().address_->ip()->ipv4()) {
      pending_response_.v4_responses_.push_back(dns_response);
    } else {
      ASSERT(dns_response.addrInfo().address_->ip()->ipv6());
      pending_response_.v6_responses_.push_back(dns_response);
    }
  }

  if (!(flags & kDNSServiceFlagsMoreComing) && isAddressFamilyProcessed(kDNSServiceProtocol_IPv4) &&
      isAddressFamilyProcessed(kDNSServiceProtocol_IPv6)) {
    ENVOY_LOG(debug, "DNS Resolver flushing queries pending callback");
    pending_response_.status_ = ResolutionStatus::Completed;
    pending_response_.details_ = absl::StrCat("apple_dns_completed_", error_code);
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
      [this](uint32_t events) {
        onEventCallback(events);
        return absl::OkStatus();
      },
      Event::FileTriggerType::Level, Event::FileReadyType::Read);
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
  }
  IS_ENVOY_BUG("unexpected DnsLookupFamily enum");
  sockaddr_in address_in;
  return {std::make_shared<const Address::Ipv4Instance>(&address_in), std::chrono::seconds(ttl)};
}

bool AppleDnsResolverImpl::PendingResolution::isAddressFamilyProcessed(
    DNSServiceProtocol protocol) {
  // If not expecting a v4/v6 query, or the v4/v6 response has been received, consider the address
  // family as having been processed.
  const bool response_received = (protocol == kDNSServiceProtocol_IPv4)
                                     ? pending_response_.v4_response_received_
                                     : pending_response_.v6_response_received_;
  return response_received || !((query_protocol_ & protocol) || query_protocol_ == 0);
}

// apple DNS resolver factory
class AppleDnsResolverFactory : public DnsResolverFactory {
public:
  std::string name() const override { return std::string(AppleDnsResolver); }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig()};
  }

  absl::StatusOr<DnsResolverSharedPtr>
  createDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                    const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config)
      const override {
    ASSERT(dispatcher.isThreadSafe());
    envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig apple;
    RETURN_IF_NOT_OK(Envoy::MessageUtil::unpackTo(typed_dns_resolver_config.typed_config(), apple));
    return std::make_shared<Network::AppleDnsResolverImpl>(apple, dispatcher, api.rootScope());
  }
};

// Register the AppleDnsResolverFactory
REGISTER_FACTORY(AppleDnsResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy

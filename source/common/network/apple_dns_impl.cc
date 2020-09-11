#include "common/network/apple_dns_impl.h"

#include <dns_sd.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/platform.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Network {

AppleDnsResolverImpl::AppleDnsResolverImpl(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {
  ENVOY_LOG(warn, "Constructing DNS resolver");
  initializeMainSdRef();
}

AppleDnsResolverImpl::~AppleDnsResolverImpl() {
  ENVOY_LOG(warn, "Destructing DNS resolver");
  // FIXME: eliminate the FilePtr from the event loop per comments from dns_sd.h?
  DNSServiceRefDeallocate(main_sd_ref_);
}

void AppleDnsResolverImpl::initializeMainSdRef() {
  auto error = DNSServiceCreateConnection(&main_sd_ref_);
  if (error) {
    ENVOY_LOG(error, "Error in DNSServiceCreateConnection");
    // throw?
  }

  auto fd = DNSServiceRefSockFD(main_sd_ref_);
  ENVOY_LOG(warn, "DNS resolver has fd={}", fd);

  // Hook up to the event loop
  sd_ref_event_ = dispatcher_.createFileEvent(
      fd, [this](uint32_t events) { onEventCallback(events); }, Event::FileTriggerType::Level,
      Event::FileReadyType::Read);

  // c-ares enables writes, but based on my read this API does not need writes?
  sd_ref_event_->setEnabled(Event::FileReadyType::Read);
}

void AppleDnsResolverImpl::onEventCallback(uint32_t events) {
  ENVOY_LOG(warn, "DNS resolver file event");
  if (events & Event::FileReadyType::Read) {
    ENVOY_LOG(warn, "DNS resolver file event READ!");
    DNSServiceProcessResult(main_sd_ref_);
  } // need to deal with other events?
}

ActiveDnsQuery* AppleDnsResolverImpl::resolve(const std::string& dns_name,
                                              DnsLookupFamily dns_lookup_family,
                                              ResolveCb callback) {
  // FIXME: think about if this is needed. Gut instinct is yes.
  // if (dirty_channel_) {
  //   dirty_channel_ = false;
  //   ares_destroy(channel_);

  //   initializeMainSdRef();
  // }
  ENVOY_LOG(warn, "DNS resolver resolve={}", dns_name);
  std::unique_ptr<PendingResolution> pending_resolution(
      new PendingResolution(*this, callback, dispatcher_, main_sd_ref_, dns_name));

  DNSServiceErrorType error = pending_resolution->dnsServiceGetAddrInfo(dns_lookup_family);

  // FIXME: deal with error.
  ASSERT(error == kDNSServiceErr_NoError);

  // FIXME: can the apple API resolve synchronously?
  // if (pending_resolution->completed_) {
  //   // Resolution does not need asynchronous behavior or network events. For
  //   // example, localhost lookup.
  //   return nullptr;
  // } else {
  //   // Enable timer to wake us up if the request times out.
  //   updateAresTimer();

  //   // The PendingResolution will self-delete when the request completes (including if cancelled
  //   or
  //   // if ~AppleDnsResolverImpl() happens via ares_destroy() and subsequent handling of
  //   ARES_EDESTRUCTION
  //   // in AppleDnsResolverImpl::PendingResolution::onAresGetAddrInfoCallback()).
  //   pending_resolution->owned_ = true;
  //   return pending_resolution.release();
  // }
  return pending_resolution.release();
}

void AppleDnsResolverImpl::PendingResolution::onDNSServiceGetAddrInfoReply(
    DNSServiceFlags flags, uint32_t interface_index, DNSServiceErrorType error_code,
    const char* hostname, const struct sockaddr* address, uint32_t ttl) {
  // CHECK:
  // destroyed
  // timed out
  // answered from cache
  // expired answer
  // do we want to enable validation?

  ASSERT(interface_index == 0);

  sockaddr_in address_in;
  memset(&address_in, 0, sizeof(address_in));
  address_in.sin_family = AF_INET;
  address_in.sin_port = 0;
  address_in.sin_addr = reinterpret_cast<const sockaddr_in*>(address)->sin_addr;

  auto envoy_address = std::make_shared<const Address::Ipv4Instance>(&address_in);
  ENVOY_LOG(warn,
            "DNS for {} resolved with: flags={}, interface_index={}, error_code={}, hostname={}, "
            "address={}, ttl={}",
            dns_name_, flags, interface_index, error_code, hostname,
            envoy_address->ip()->addressAsString(), ttl);
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
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  // Do we want to allow expired answers from the cache?
  // Do we want validation?
  return DNSServiceGetAddrInfo(
      &individual_sd_ref_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0, protocol,
      dns_name_.c_str(),
      [](DNSServiceRef, DNSServiceFlags flags, uint32_t interface_index,
         DNSServiceErrorType error_code, const char* hostname, const struct sockaddr* address,
         uint32_t ttl, void* context) {
        static_cast<PendingResolution*>(context)->onDNSServiceGetAddrInfoReply(
            flags, interface_index, error_code, hostname, address, ttl);
      },
      this);
}

} // namespace Network
} // namespace Envoy

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
  deallocateMainSdRef();
}

void AppleDnsResolverImpl::deallocateMainSdRef() {
  ENVOY_LOG(warn, "DNSServiceRefDeallocate main sd ref");
  // dns_sd.h says:
  //   If the reference's underlying socket is used in a run loop or select() call, it should
  //   be removed BEFORE DNSServiceRefDeallocate() is called, as this function closes the
  //   reference's socket.
  sd_ref_event_.reset();
  DNSServiceRefDeallocate(main_sd_ref_);
}

void AppleDnsResolverImpl::initializeMainSdRef() {
  auto error = DNSServiceCreateConnection(&main_sd_ref_);
  if (error) {
    throw EnvoyException("Error in DNSServiceCreateConnection");
  }

  auto fd = DNSServiceRefSockFD(main_sd_ref_);
  if (fd == -1) {
    throw EnvoyException("Error in DNSServiceRefSockFD");
  }
  ENVOY_LOG(warn, "DNS resolver has fd={}", fd);

  sd_ref_event_ = dispatcher_.createFileEvent(
      fd, [this](uint32_t events) { onEventCallback(events); }, Event::FileTriggerType::Level,
      Event::FileReadyType::Read);
  sd_ref_event_->setEnabled(Event::FileReadyType::Read);
}

void AppleDnsResolverImpl::onEventCallback(uint32_t events) {
  ENVOY_LOG(warn, "DNS resolver file event");
  if (events & Event::FileReadyType::Read) {
    DNSServiceProcessResult(main_sd_ref_);
  }
}

ActiveDnsQuery* AppleDnsResolverImpl::resolve(const std::string& dns_name,
                                              DnsLookupFamily dns_lookup_family,
                                              ResolveCb callback) {
  ENVOY_LOG(warn, "DNS resolver resolve={}", dns_name);
  std::unique_ptr<PendingResolution> pending_resolution(
      new PendingResolution(*this, callback, dispatcher_, main_sd_ref_, dns_name));

  DNSServiceErrorType error = pending_resolution->dnsServiceGetAddrInfo(dns_lookup_family);
  if (error != kDNSServiceErr_NoError) {
    return nullptr;
  }

  // If the query was synchronously resolved, there is no need to return the query.
  // FIXME: I don't think synchronous resolution is possible from my read of dns_sd.h,
  // and tests with local resolution.
  // What about with caching?
  if (pending_resolution->synchronously_completed_) {
    return nullptr;
  }

  pending_resolution->owned_ = true;
  return pending_resolution.release();
}

void AppleDnsResolverImpl::flushPendingQueries() {
  ENVOY_LOG(warn, "DNS Resolver flushing {} queries", queries_with_pending_cb_.size());
  for (std::list<PendingResolution*>::iterator it = queries_with_pending_cb_.begin();
       it != queries_with_pending_cb_.end(); ++it) {
    try {
      ASSERT((*it)->pending_cb_);
      (*it)->callback_((*it)->pending_cb_->status_, std::move((*it)->pending_cb_->responses_));
    } catch (const EnvoyException& e) {
      ENVOY_LOG(critical, "EnvoyException in DNSService callback: {}", e.what());
      (*it)->dispatcher_.post([s = std::string(e.what())] { throw EnvoyException(s); });
    } catch (const std::exception& e) {
      ENVOY_LOG(critical, "std::exception in DNSService callback: {}", e.what());
      (*it)->dispatcher_.post([s = std::string(e.what())] { throw EnvoyException(s); });
    } catch (...) {
      ENVOY_LOG(critical, "Unknown exception in DNSService callback");
      (*it)->dispatcher_.post([] { throw EnvoyException("unknown"); });
    }

    if ((*it)->owned_) {
      ENVOY_LOG(warn, "Resolution for {} completed (async)", (*it)->dns_name_);
      delete *it;
    } else {
      ENVOY_LOG(warn, "Resolution for {} completed (synchronously)", (*it)->dns_name_);
      (*it)->synchronously_completed_ = true;
    }
  }

  // Purge the contents so no one tries to delete them again.
  queries_with_pending_cb_.clear();
}

AppleDnsResolverImpl::PendingResolution::~PendingResolution() {
  ENVOY_LOG(warn, "Destroying PendingResolution for {}", dns_name_);
  DNSServiceRefDeallocate(individual_sd_ref_);
}

void AppleDnsResolverImpl::PendingResolution::cancel() {
  ENVOY_LOG(warn, "Cancelling PendingResolution for {}", dns_name_);
  ASSERT(owned_);
  if (pending_cb_) {
    parent_.queries_with_pending_cb_.remove(this);
  }
  delete this;
}

void AppleDnsResolverImpl::PendingResolution::onDNSServiceGetAddrInfoReply(
    DNSServiceFlags flags, uint32_t interface_index, DNSServiceErrorType error_code,
    const char* hostname, const struct sockaddr* address, uint32_t ttl) {
  ENVOY_LOG(warn,
            "DNS for {} resolved with: flags={}[MoreComing={}, Add={}], interface_index={}, "
            "error_code={}, hostname={}",
            dns_name_, flags, flags & kDNSServiceFlagsMoreComing ? "yes" : "no",
            flags & kDNSServiceFlagsAdd ? "yes" : "no", interface_index, error_code, hostname);
  ASSERT(interface_index == 0);

  if (error_code == kDNSServiceErr_DefunctConnection || error_code == kDNSServiceErr_Timeout || error_code == kDNSServiceErr_Refused) {
    // Current query gets a failure status
    if (!pending_cb_) {
      ENVOY_LOG(warn, "[Error path] Adding to queries pending callback");
      pending_cb_ = {ResolutionStatus::Failure, {}};
      parent_.addPendingQuery(this);
    } else {
      ENVOY_LOG(warn, "[Error path] Changing status for query already pending flush");
      pending_cb_->status_ = ResolutionStatus::Failure;
    }

    ENVOY_LOG(warn, "[Error path] DNS Resolver flushing queries pending callback");
    parent_.flushPendingQueries();

    // Deallocation of the MainSdRef __has__ to happend __after__ flushing queries. Flushing queries
    // deallocates individual refs, so deallocating the main ref aheadd would cause deallocation of
    // invalid individual refs per dns_sd.h
    parent_.deallocateMainSdRef();
    // POINT FOR DISCUSSION: can this throw here? Or does it need to be posted like the callback in
    // flushPendingQueries.
    parent_.initializeMainSdRef();

    return;
  }

  // At this point all known non-success cases have been dealt with. Assert that the error code is
  // no error.
  // POINT FOR DISCUSSION: Crash otherwise to alert consumers that there are unadressed edge cases?
  // stat?
  RELEASE_ASSERT(error_code == kDNSServiceErr_NoError,
                 "An unknown error has been returned by DNSServiceGetAddrInfoReply");

  // Only add this address to the list if kDNSServiceFlagsAdd is set. Callback targets are purely
  // additive.
  if (flags & kDNSServiceFlagsAdd) {
    auto dns_response = buildDnsResponse(address, ttl);
    ENVOY_LOG(warn, "Address to add address={}, ttl={}",
              dns_response.address_->ip()->addressAsString(), ttl);

    if (!pending_cb_) {
      ENVOY_LOG(warn, "Adding to queries pending callback");
      pending_cb_ = {ResolutionStatus::Success, {dns_response}};
      parent_.addPendingQuery(this);
    } else {
      ENVOY_LOG(warn, "New address for query already pending flush");
      pending_cb_->responses_.push_back(dns_response);
    }
  }

  if (!(flags & kDNSServiceFlagsMoreComing)) {
    ENVOY_LOG(warn, "DNS Resolver flushing queries pending callback");
    parent_.flushPendingQueries();
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
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  // POINT FOR DISCUSSION: Do we want to allow caching, allow expired answers from the cache?
  // POINT FOR DISCUSSION: Do we want validation DNSSEC?
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

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
  ENVOY_LOG(debug, "Constructing DNS resolver");
  initializeMainSdRef();
}

AppleDnsResolverImpl::~AppleDnsResolverImpl() {
  ENVOY_LOG(debug, "Destructing DNS resolver");
  deallocateMainSdRef();
}

void AppleDnsResolverImpl::deallocateMainSdRef() {
  ENVOY_LOG(debug, "DNSServiceRefDeallocate main sd ref");
  // dns_sd.h says:
  //   If the reference's underlying socket is used in a run loop or select() call, it should
  //   be removed BEFORE DNSServiceRefDeallocate() is called, as this function closes the
  //   reference's socket.
  sd_ref_event_.reset();
  DNSServiceRefDeallocate(main_sd_ref_);
}

void AppleDnsResolverImpl::initializeMainSdRef() {
  // This implementation uses a shared connection for three main reasons:
  //    1. Efficiency of concurrent resolutions by sharing the same underlying UDS to the DNS
  //       server.
  //    2. An error on a connection to the DNS server is good indication that other connections,
  //       even if not shared, would not succeed. So it is better to share one connection and
  //       promptly cancel all outstanding queries, rather than individually wait for all
  //       connections to error out.
  //    3. It follows the precedent set in dns_impl with the c-ares library, for consistency of
  //       style, performance, and expectations between the two implementations.
  // However, using a shared connection brings some complexities detailed in the inline comments
  // for kDNSServiceFlagsShareConnection in dns_sd.h, and copied (and edited) in this implementation
  // where relevant.
  auto error = DNSServiceCreateConnection(&main_sd_ref_);
  RELEASE_ASSERT(!error, fmt::format("error ({}) in DNSServiceCreateConnection", error));

  auto fd = DNSServiceRefSockFD(main_sd_ref_);
  RELEASE_ASSERT(fd != -1, "error in DNSServiceRefSockFD");
  ENVOY_LOG(debug, "DNS resolver has fd={}", fd);

  sd_ref_event_ = dispatcher_.createFileEvent(
      fd,
      // note: Event::FileTriggerType::Level is used here to closely resemble the c-ares
      // implementation in dns_impl.cc.
      [this](uint32_t events) { onEventCallback(events); }, Event::FileTriggerType::Level,
      Event::FileReadyType::Read);
  sd_ref_event_->setEnabled(Event::FileReadyType::Read);
}

void AppleDnsResolverImpl::onEventCallback(uint32_t events) {
  ENVOY_LOG(debug, "DNS resolver file event ({})", events);
  ASSERT(events & Event::FileReadyType::Read);
  DNSServiceErrorType error = DNSServiceProcessResult(main_sd_ref_);
  if (error != kDNSServiceErr_NoError) {
    ENVOY_LOG(warn, "DNS resolver error ({}) in DNSServiceProcessResult", error);
    // Similar to receiving an error in onDNSServiceGetAddrInfoReply, an error while processing fd
    // events indicates that the sd_ref state is broken.
    // Therefore, flush queries with_error == true.
    flushPendingQueries(true /* with_error */);
  }
}

ActiveDnsQuery* AppleDnsResolverImpl::resolve(const std::string& dns_name,
                                              DnsLookupFamily dns_lookup_family,
                                              ResolveCb callback) {
  ENVOY_LOG(debug, "DNS resolver resolve={}", dns_name);
  std::unique_ptr<PendingResolution> pending_resolution(
      new PendingResolution(*this, callback, dispatcher_, main_sd_ref_, dns_name));

  DNSServiceErrorType error = pending_resolution->dnsServiceGetAddrInfo(dns_lookup_family);
  if (error != kDNSServiceErr_NoError) {
    ENVOY_LOG(warn, "DNS resolver error ({}) in dnsServiceGetAddrInfo for {}", error, dns_name);
    return nullptr;
  }

  // If the query was synchronously resolved, there is no need to return the query.
  if (pending_resolution->synchronously_completed_) {
    return nullptr;
  }

  pending_resolution->owned_ = true;
  return pending_resolution.release();
}

void AppleDnsResolverImpl::addPendingQuery(PendingResolution* query) {
  ASSERT(queries_with_pending_cb_.count(query) == 0);
  queries_with_pending_cb_.insert(query);
}

void AppleDnsResolverImpl::removePendingQuery(PendingResolution* query) {
  auto erased = queries_with_pending_cb_.erase(query);
  ASSERT(erased == 1);
}

void AppleDnsResolverImpl::flushPendingQueries(const bool with_error) {
  ENVOY_LOG(debug, "DNS Resolver flushing {} queries", queries_with_pending_cb_.size());
  for (std::set<PendingResolution*>::iterator it = queries_with_pending_cb_.begin();
       it != queries_with_pending_cb_.end(); ++it) {
    auto query = *it;
    try {
      ASSERT(query->pending_cb_);
      query->callback_(query->pending_cb_->status_, std::move(query->pending_cb_->responses_));
    } catch (const std::exception& e) {
      ENVOY_LOG(warn, "std::exception in DNSService callback: {}", e.what());
      throw EnvoyException(e.what());
    } catch (...) {
      ENVOY_LOG(warn, "Unknown exception in DNSService callback");
      throw EnvoyException("unknown");
    }

    if (query->owned_) {
      ENVOY_LOG(debug, "Resolution for {} completed (async)", query->dns_name_);
      delete *it;
    } else {
      ENVOY_LOG(debug, "Resolution for {} completed (synchronously)", query->dns_name_);
      query->synchronously_completed_ = true;
    }
  }

  // Purge the contents so no one tries to delete them again.
  queries_with_pending_cb_.clear();

  if (with_error) {
    // The main sd ref is destroyed here because a callback with an error is good indication that
    // the connection to the DNS server is faulty and needs to be torn down.
    //
    // Deallocation of the MainSdRef __has__ to happen __after__ flushing queries. Flushing queries
    // de-allocates individual refs, so deallocating the main ref ahead would cause deallocation of
    // invalid individual refs per dns_sd.h
    deallocateMainSdRef();
    initializeMainSdRef();
  }
}

AppleDnsResolverImpl::PendingResolution::~PendingResolution() {
  ENVOY_LOG(debug, "Destroying PendingResolution for {}", dns_name_);
  // It is possible that DNSServiceGetAddrInfo returns a synchronous error, with a NULLed
  // DNSServiceRef, in AppleDnsResolverImpl::resolve.
  // Additionally, it is also possible that the query is cancelled before resolution starts, and
  // thus the DNSServiceRef is null.
  // Therefore, only deallocate if the ref is not null.
  if (individual_sd_ref_) {
    DNSServiceRefDeallocate(individual_sd_ref_);
  }
}

void AppleDnsResolverImpl::PendingResolution::cancel() {
  ENVOY_LOG(debug, "Cancelling PendingResolution for {}", dns_name_);
  ASSERT(owned_);
  if (pending_cb_) {
    /* (taken and edited from dns_sd.h)
     * Canceling operations and kDNSServiceFlagsMoreComing
     * Whenever you cancel any operation for which you had deferred [resolution]
     * because of a kDNSServiceFlagsMoreComing flag, you should [flush]. This is because, after
     * cancelling the operation, you can no longer wait for a callback *without* MoreComing set, to
     * tell you [to flush] (the operation has been canceled, so there will be no more callbacks).
     *
     * [FURTHER] An implication of the collective
     * kDNSServiceFlagsMoreComing flag for shared connections is that this
     * guideline applies more broadly -- any time you cancel an operation on
     * a shared connection, you should perform all deferred updates for all
     * operations sharing that connection. This is because the MoreComing flag
     * might have been referring to events coming for the operation you canceled,
     * which will now not be coming because the operation has been canceled.
     */
    // First, get rid of the current query, because if it is canceled, its callback should not be
    // executed during the subsequent flush.
    parent_.removePendingQuery(this);
    // Then, flush all other queries.
    parent_.flushPendingQueries(false /* with_error */);
  }
  // Because the query is self-owned, delete now.
  delete this;
}

void AppleDnsResolverImpl::PendingResolution::onDNSServiceGetAddrInfoReply(
    DNSServiceFlags flags, uint32_t interface_index, DNSServiceErrorType error_code,
    const char* hostname, const struct sockaddr* address, uint32_t ttl) {
  ENVOY_LOG(debug,
            "DNS for {} resolved with: flags={}[MoreComing={}, Add={}], interface_index={}, "
            "error_code={}, hostname={}",
            dns_name_, flags, flags & kDNSServiceFlagsMoreComing ? "yes" : "no",
            flags & kDNSServiceFlagsAdd ? "yes" : "no", interface_index, error_code, hostname);
  ASSERT(interface_index == 0);

  // Generic error handling.
  if (error_code != kDNSServiceErr_NoError) {
    // TODO(junr03): consider creating stats for known error types (timeout, refused connection,
    // etc.). Currently a bit challenging because there is no scope access wired through. Current
    // query gets a failure status
    if (!pending_cb_) {
      ENVOY_LOG(warn, "[Error path] Adding to queries pending callback");
      pending_cb_ = {ResolutionStatus::Failure, {}};
      parent_.addPendingQuery(this);
    } else {
      ENVOY_LOG(warn, "[Error path] Changing status for query already pending flush");
      pending_cb_->status_ = ResolutionStatus::Failure;
    }

    ENVOY_LOG(warn, "[Error path] DNS Resolver flushing queries pending callback");
    parent_.flushPendingQueries(true /* with_error */);
    // Note: Nothing can follow this call to flushPendingQueries due to deletion of this
    // object upon resolution.
    return;
  }

  // Only add this address to the list if kDNSServiceFlagsAdd is set. Callback targets are purely
  // additive.
  if (flags & kDNSServiceFlagsAdd) {
    auto dns_response = buildDnsResponse(address, ttl);
    ENVOY_LOG(debug, "Address to add address={}, ttl={}",
              dns_response.address_->ip()->addressAsString(), ttl);

    if (!pending_cb_) {
      ENVOY_LOG(debug, "Adding to queries pending callback");
      pending_cb_ = {ResolutionStatus::Success, {dns_response}};
      parent_.addPendingQuery(this);
    } else {
      ENVOY_LOG(debug, "New address for query already pending flush");
      pending_cb_->responses_.push_back(dns_response);
    }
  }

  if (!(flags & kDNSServiceFlagsMoreComing)) {
    /* (taken and edited from dns_sd.h)
     * Collective kDNSServiceFlagsMoreComing flag:
     * When [DNSServiceGetAddrInfoReply] are invoked using a shared DNSServiceRef, the
     * kDNSServiceFlagsMoreComing flag applies collectively to *all* active
     * operations sharing the same [main_sd_ref]. If the MoreComing flag is
     * set it means that there are more results queued on this parent DNSServiceRef,
     * but not necessarily more results for this particular callback function.
     * The implication of this for client programmers is that when a callback
     * is invoked with the MoreComing flag set, the code should update its
     * internal data structures with the new result (as is done above when calling
     * parent_.addPendingQuery(this))...Then, later when a callback is eventually invoked with the
     * MoreComing flag not set, the code should update *all* [pending queries] related to that
     * shared parent DNSServiceRef that need updating (i.e that have had DNSServiceGetAddrInfoReply
     * called on them since the last flush), not just the [queries] related to the particular
     * callback that happened to be the last one to be invoked.
     */
    ENVOY_LOG(debug, "DNS Resolver flushing queries pending callback");
    parent_.flushPendingQueries(false /* with_error */);
    // Note: Nothing can follow this call to flushPendingQueries due to deletion of this
    // object upon resolution.
    return;
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
  // TODO: explore validation via DNSSEC?
  return DNSServiceGetAddrInfo(
      &individual_sd_ref_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0, protocol,
      dns_name_.c_str(),
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

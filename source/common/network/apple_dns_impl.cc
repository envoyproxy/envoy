#include "common/network/apple_dns_impl.h"

#include <dns_sd.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/event/file_event.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

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

// Parameters of the jittered backoff strategy.
static constexpr std::chrono::milliseconds RetryInitialDelayMilliseconds(30);
static constexpr std::chrono::milliseconds RetryMaxDelayMilliseconds(30000);

AppleDnsResolverImpl::AppleDnsResolverImpl(Event::Dispatcher& dispatcher,
                                           Random::RandomGenerator& random,
                                           Stats::Scope& root_scope)
    : dispatcher_(dispatcher), initialize_failure_timer_(dispatcher.createTimer(
                                   [this]() -> void { initializeMainSdRef(); })),
      backoff_strategy_(std::make_unique<JitteredExponentialBackOffStrategy>(
          RetryInitialDelayMilliseconds.count(), RetryMaxDelayMilliseconds.count(), random)),
      scope_(root_scope.createScope("dns.apple.")), stats_(generateAppleDnsResolverStats(*scope_)) {
  ENVOY_LOG(debug, "Constructing DNS resolver");
  initializeMainSdRef();
}

AppleDnsResolverImpl::~AppleDnsResolverImpl() { deallocateMainSdRef(); }

AppleDnsResolverStats AppleDnsResolverImpl::generateAppleDnsResolverStats(Stats::Scope& scope) {
  return {ALL_APPLE_DNS_RESOLVER_STATS(POOL_COUNTER(scope))};
}

void AppleDnsResolverImpl::deallocateMainSdRef() {
  ENVOY_LOG(debug, "DNSServiceRefDeallocate main sd ref");
  // dns_sd.h says:
  //   If the reference's underlying socket is used in a run loop or select() call, it should
  //   be removed BEFORE DNSServiceRefDeallocate() is called, as this function closes the
  //   reference's socket.
  sd_ref_event_.reset();
  DnsServiceSingleton::get().dnsServiceRefDeallocate(main_sd_ref_);
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
  //
  // When error occurs while the main_sd_ref_ is initialized, the initialize_failure_timer_ will be
  // enabled to retry initialization. Retries can also be triggered via query submission, @see
  // AppleDnsResolverImpl::resolve(...) for details.
  auto error = DnsServiceSingleton::get().dnsServiceCreateConnection(&main_sd_ref_);
  if (error != kDNSServiceErr_NoError) {
    stats_.connection_failure_.inc();
    initialize_failure_timer_->enableTimer(
        std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
    return;
  }

  auto fd = DnsServiceSingleton::get().dnsServiceRefSockFD(main_sd_ref_);
  // According to dns_sd.h: DnsServiceRefSockFD returns "The DNSServiceRef's underlying socket
  // descriptor, or -1 on error.". Although it gives no detailed description on when/why this call
  // would fail.
  if (fd == -1) {
    stats_.socket_failure_.inc();
    initialize_failure_timer_->enableTimer(
        std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
    return;
  }

  sd_ref_event_ = dispatcher_.createFileEvent(
      fd,
      // note: Event::FileTriggerType::Level is used here to closely resemble the c-ares
      // implementation in dns_impl.cc.
      [this](uint32_t events) { onEventCallback(events); }, Event::FileTriggerType::Level,
      Event::FileReadyType::Read);
  sd_ref_event_->setEnabled(Event::FileReadyType::Read);

  // Disable the failure timer and reset the backoff strategy because the main_sd_ref_ was
  // successfully initialized. Note that these actions will be no-ops if the timer was not armed to
  // begin with.
  initialize_failure_timer_->disableTimer();
  backoff_strategy_->reset();
}

void AppleDnsResolverImpl::onEventCallback(uint32_t events) {
  ENVOY_LOG(debug, "DNS resolver file event ({})", events);
  RELEASE_ASSERT(events & Event::FileReadyType::Read,
                 fmt::format("invalid FileReadyType event={}", events));
  DNSServiceErrorType error = DnsServiceSingleton::get().dnsServiceProcessResult(main_sd_ref_);
  if (error != kDNSServiceErr_NoError) {
    ENVOY_LOG(warn, "DNS resolver error ({}) in DNSServiceProcessResult", error);
    stats_.processing_failure_.inc();
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

    // First check that the main_sd_ref is alive by checking if the resolver is currently trying to
    // initialize its main_sd_ref.
    if (initialize_failure_timer_->enabled()) {
      // No queries should be accumulating while the main_sd_ref_ is not alive. Either they were
      // flushed when the error that deallocated occurred, or they have all failed in this branch of
      // the code synchronously due to continuous inability to initialize the main_sd_ref_.
      ASSERT(queries_with_pending_cb_.empty());

      // Short-circuit the pending retry to initialize the main_sd_ref_ and try now.
      initializeMainSdRef();

      // If the timer is still enabled, that means the initialization failed. Synchronously fail the
      // resolution, the callback target should retry.
      if (initialize_failure_timer_->enabled()) {
        callback(DnsResolver::ResolutionStatus::Failure, {});
        return nullptr;
      }
    }

    // Proceed with resolution after establishing that the resolver has a live main_sd_ref_.
    std::unique_ptr<PendingResolution> pending_resolution(
        new PendingResolution(*this, callback, dispatcher_, main_sd_ref_, dns_name));

    DNSServiceErrorType error = pending_resolution->dnsServiceGetAddrInfo(dns_lookup_family);
    if (error != kDNSServiceErr_NoError) {
      ENVOY_LOG(warn, "DNS resolver error ({}) in dnsServiceGetAddrInfo for {}", error, dns_name);
      return nullptr;
    }

    // If the query was synchronously resolved in the Apple API call, there is no need to return the
    // query.
    if (pending_resolution->synchronously_completed_) {
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

    ASSERT(query->pending_cb_);
    query->callback_(query->pending_cb_->status_, std::move(query->pending_cb_->responses_));

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
    ENVOY_LOG(debug, "DNSServiceRefDeallocate individual sd ref");
    DnsServiceSingleton::get().dnsServiceRefDeallocate(individual_sd_ref_);
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

  if (!pending_cb_) {
    pending_cb_ = {ResolutionStatus::Success, {}};
    parent_.addPendingQuery(this);
  }

  // Generic error handling.
  if (error_code != kDNSServiceErr_NoError) {
    // TODO(junr03): consider creating stats for known error types (timeout, refused connection,
    // etc.). Currently a bit challenging because there is no scope access wired through. Current
    // query gets a failure status

    pending_cb_->status_ = ResolutionStatus::Failure;
    pending_cb_->responses_.clear();

    ENVOY_LOG(warn, "[Error path] DNS Resolver flushing queries pending callback");
    parent_.flushPendingQueries(true /* with_error */);
    // Note: Nothing can follow this call to flushPendingQueries due to deletion of this
    // object upon resolution.
    return;
  }

  // Only add this address to the list if kDNSServiceFlagsAdd is set. Callback targets are only
  // additive.
  if (flags & kDNSServiceFlagsAdd) {
    ASSERT(address, "invalid to add null address");
    auto dns_response = buildDnsResponse(address, ttl);
    ENVOY_LOG(debug, "Address to add address={}, ttl={}",
              dns_response.address_->ip()->addressAsString(), ttl);
    pending_cb_->responses_.push_back(dns_response);
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
  // TODO: explore validation via `DNSSEC`?
  return DnsServiceSingleton::get().dnsServiceGetAddrInfo(
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

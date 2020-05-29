#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"

#include "extensions/filters/udp/dns_filter/dns_parser.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

enum class DnsFilterResolverStatus { Pending, Complete, TimedOut };

/*
 * This class encapsulates the logic of handling an asynchronous DNS request for the DNS filter.
 * External request timeouts are handled here.
 */
class DnsFilterResolver : Logger::Loggable<Logger::Id::filter> {
public:
  DnsFilterResolver(DnsFilterResolverCallback& callback, AddressConstPtrVec resolvers,
                    std::chrono::milliseconds timeout, Event::Dispatcher& dispatcher)
      : resolver_(dispatcher.createDnsResolver(resolvers, false /* use_tcp_for_dns_lookups */)),
        callback_(callback), timeout_(timeout),
        timeout_timer_(dispatcher.createTimer([this]() -> void { onResolveTimeout(); })),
        active_query_(nullptr) {}

  /**
   * @brief entry point to resolve the name in a DnsQueryRecord
   *
   * This function uses the query object to determine whether it is requesting an A or AAAA record
   * for the given name. When the resolver callback executes, this will execute a DNS Filter
   * callback in order to build the answer object returned to the client.
   *
   * @param domain_query the query record object containing the name for which we are resolving
   */
  void resolveExternalQuery(DnsQueryContextPtr context, const DnsQueryRecord* domain_query);

private:
  /**
   * @brief invokes the DNS Filter callback only if our state indicates we have not timed out
   * waiting for a response from the external resolver
   */
  void invokeCallback(Network::DnsResolver::ResolutionStatus status) {
    // We've timed out. Guard against sending a response
    if (resolution_status_ == DnsFilterResolverStatus::TimedOut) {
      return;
    }

    timeout_timer_->disableTimer();
    external_context_->resolver_status_ = status;
    callback_(std::move(external_context_), query_rec_, resolved_hosts_);
  }

  /**
   * @brief Invoke the DNS Filter callback after explicitly clearing the resolved hosts list. The
   * DNS Filter will respond to the client appropriately.
   */
  void onResolveTimeout() {
    // Guard against executing the filter callback and sending a response
    if (resolution_status_ != DnsFilterResolverStatus::Pending) {
      return;
    }
    resolution_status_ = DnsFilterResolverStatus::TimedOut;
    resolved_hosts_.clear();
    external_context_->resolver_status_ = Network::DnsResolver::ResolutionStatus::Failure;
    callback_(std::move(external_context_), query_rec_, resolved_hosts_);
  }

  const Network::DnsResolverSharedPtr resolver_;
  DnsFilterResolverCallback& callback_;
  std::chrono::milliseconds timeout_;
  Event::TimerPtr timeout_timer_;
  const DnsQueryRecord* query_rec_;
  Network::ActiveDnsQuery* active_query_;
  DnsFilterResolverStatus resolution_status_;
  AddressConstPtrVec resolved_hosts_;
  DnsQueryContextPtr external_context_;
};

using DnsFilterResolverPtr = std::unique_ptr<DnsFilterResolver>;

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

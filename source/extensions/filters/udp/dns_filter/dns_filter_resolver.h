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
                    std::chrono::milliseconds timeout, Event::Dispatcher& dispatcher,
                    uint64_t max_pending_lookups)
      : dispatcher_(dispatcher),
        resolver_(dispatcher.createDnsResolver(resolvers, false /* use_tcp_for_dns_lookups */)),
        callback_(callback), timeout_(timeout), max_pending_lookups_(max_pending_lookups) {}
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
  struct LookupContext {
    const DnsQueryRecord* query_rec;
    DnsQueryContextPtr query_context;
    uint64_t expiry;
    AddressConstPtrVec resolved_hosts;
    DnsFilterResolverStatus resolver_status;
    Event::TimerPtr timeout_timer;
  };
  /**
   * @brief invokes the DNS Filter callback only if our state indicates we have not timed out
   * waiting for a response from the external resolver
   */
  void invokeCallback(LookupContext& context) {
    // If we've timed out. Guard against sending a response
    if (context.resolver_status == DnsFilterResolverStatus::Complete) {
      callback_(std::move(context.query_context), context.query_rec, context.resolved_hosts);
    }
  }

  /**
   * @brief Invoke the DNS Filter callback to send a response to a client if the query has timed out
   * DNS Filter will respond to the client appropriately.
   */
  void onResolveTimeout();

  Event::Dispatcher& dispatcher_;
  const Network::DnsResolverSharedPtr resolver_;
  DnsFilterResolverCallback& callback_;
  std::chrono::milliseconds timeout_;
  absl::flat_hash_map<const DnsQueryRecord*, LookupContext> lookups_;
  uint64_t max_pending_lookups_;
};

using DnsFilterResolverPtr = std::unique_ptr<DnsFilterResolver>;

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

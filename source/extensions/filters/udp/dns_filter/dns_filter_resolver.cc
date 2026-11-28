#include "source/extensions/filters/udp/dns_filter/dns_filter_resolver.h"

#include <utility>

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

DnsFilterResolver::~DnsFilterResolver() {
  // Detach the queries before cancellation, which can call back into the resolver.
  for (auto& lookup : std::exchange(lookups_, {})) {
    if (lookup.second.active_query != nullptr) {
      lookup.second.active_query->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
    }
  }
}

void DnsFilterResolver::resolveExternalQuery(DnsQueryContextPtr context,
                                             const DnsQueryRecord* domain_query) {
  // Create an external resolution context for the query.
  LookupContext ctx{};
  ctx.query_rec = domain_query;
  ctx.query_context = std::move(context);
  ctx.query_context->in_callback_ = false;

  Network::DnsLookupFamily lookup_family;
  switch (domain_query->type_) {
  case DNS_RECORD_TYPE_A:
    lookup_family = Network::DnsLookupFamily::V4Only;
    break;
  case DNS_RECORD_TYPE_AAAA:
    lookup_family = Network::DnsLookupFamily::V6Only;
    break;
  default:
    // We don't support other lookups other than A and AAAA. Set success here so that we don't
    // retry for something that we are certain will fail.
    ENVOY_LOG(debug, "Unknown query type [{}] for upstream lookup", domain_query->type_);
    ctx.query_context->resolution_status_ = Network::DnsResolver::ResolutionStatus::Completed;
    invokeCallback(ctx);
    return;
  }

  const DnsQueryRecord* id = domain_query;

  // If we have too many pending lookups, invoke the callback to retry the query.
  if (lookups_.size() > max_pending_lookups_) {
    ENVOY_LOG(
        trace,
        "Retrying query for [{}] because there are too many pending lookups: [pending {}/max {}]",
        domain_query->name_, lookups_.size(), max_pending_lookups_);
    invokeCallback(ctx);
    return;
  }

  ctx.timeout_timer = dispatcher_.createTimer([this, id]() { onResolveTimeout(id); });
  ctx.timeout_timer->enableTimer(timeout_);

  lookups_.emplace(id, std::move(ctx));

  ENVOY_LOG(trace, "Pending queries: {}", lookups_.size());

  // Define the callback that is executed when resolution completes
  // Resolve the address in the query and add to the resolved_hosts vector
  auto* active_query = resolver_->resolve(
      domain_query->name_, lookup_family,
      [this, id](Network::DnsResolver::ResolutionStatus status, absl::string_view,
                 std::list<Network::DnsResponse>&& response) -> void {
        auto ctx_iter = lookups_.find(id);

        // If the context is not in the map, the lookup has timed out and was removed
        // when the timer executed
        if (ctx_iter == lookups_.end()) {
          ENVOY_LOG(debug, "Unable to find context for DNS query for ID [{}]",
                    reinterpret_cast<intptr_t>(id));
          return;
        }

        auto ctx = std::move(ctx_iter->second);
        lookups_.erase(ctx_iter->first);

        // We are processing the response here, so we did not timeout. Cancel the
        // timer
        ctx.timeout_timer->disableTimer();

        ENVOY_LOG(trace, "async query status returned for query [{}]. Entries {}",
                  ctx.query_context->id_, response.size());

        ctx.query_context->in_callback_ = true;
        ctx.query_context->resolution_status_ = status;

        // C-ares doesn't expose the TTL in the data available here.
        if (status == Network::DnsResolver::ResolutionStatus::Completed) {
          ctx.resolved_hosts.reserve(response.size());
          for (const auto& resp : response) {
            const auto& addrinfo = resp.addrInfo();
            ASSERT(addrinfo.address_ != nullptr);
            ENVOY_LOG(trace, "Resolved address: {} for {}",
                      addrinfo.address_->ip()->addressAsString(), ctx.query_rec->name_);
            ctx.resolved_hosts.emplace_back(std::move(addrinfo.address_));
          }
        }
        // Invoke the filter callback notifying it of resolved addresses
        invokeCallback(ctx);
      });
  // Synchronous completion removes the lookup before resolve() returns.
  auto ctx_iter = lookups_.find(id);
  if (ctx_iter != lookups_.end()) {
    ctx_iter->second.active_query = active_query;
  }
}

void DnsFilterResolver::onResolveTimeout(const DnsQueryRecord* id) {
  auto it = lookups_.find(id);
  if (it == lookups_.end()) {
    return;
  }
  auto ctx = std::move(it->second);
  lookups_.erase(it);
  ENVOY_LOG(trace, "Purging expired query: {}", id->name_);
  ctx.query_context->resolution_status_ = Network::DnsResolver::ResolutionStatus::Failure;
  if (ctx.active_query != nullptr) {
    ctx.active_query->cancel(Network::ActiveDnsQuery::CancelReason::Timeout);
  }
  invokeCallback(ctx);
}
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

#include "extensions/filters/udp/dns_filter/dns_filter_resolver.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

void DnsFilterResolver::resolveExternalQuery(DnsQueryContextPtr context,
                                             const DnsQueryRecord* domain_query) {
  if (active_query_ != nullptr) {
    active_query_->cancel();
    active_query_ = nullptr;
  }

  // Create an external resolution context for the query.
  LookupContext ctx{};
  ctx.query_rec = domain_query;
  ctx.query_context = std::move(context);
  ctx.expiry = std::chrono::duration_cast<std::chrono::seconds>(
                   dispatcher_.timeSource().systemTime().time_since_epoch())
                   .count() +
               std::chrono::duration_cast<std::chrono::seconds>(timeout_).count();
  ctx.resolver_status = DnsFilterResolverStatus::Pending;

  timeout_timer_->disableTimer();

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
    ctx.query_context->resolution_status_ = Network::DnsResolver::ResolutionStatus::Success;
    invokeCallback(ctx);
    return;
  }

  const uint16_t id = ctx.query_context->id_;
  if (!lookups_.contains(id)) {
    lookups_.emplace(id, std::move(ctx));
  }
  ENVOY_LOG(trace, "Pending queries: {}", lookups_.size());

  // Re-arm the timeout timer
  timeout_timer_->enableTimer(timeout_);

  // Define the callback that is executed when resolution completes
  auto resolve_cb = [this, id](Network::DnsResolver::ResolutionStatus status,
                               std::list<Network::DnsResponse>&& response) -> void {
    active_query_ = nullptr;
    auto ctx_iter = lookups_.find(id);
    if (ctx_iter == lookups_.end()) {
      ENVOY_LOG(debug, "Unable to find context for DNS query for ID [{}]", id);
      return;
    }

    // We are processing the response here, so we did not timeout. Cancel the timer
    timeout_timer_->disableTimer();

    auto ctx = std::move(ctx_iter->second);
    lookups_.erase(ctx_iter->first);

    ENVOY_LOG(trace, "async query status returned. Entries {}", response.size());
    if (ctx.resolver_status != DnsFilterResolverStatus::Pending) {
      ENVOY_LOG(debug, "Resolution timed out before callback was executed");
      return;
    }

    ctx.query_context->resolution_status_ = status;
    ctx.resolver_status = DnsFilterResolverStatus::Complete;

    // C-ares doesn't expose the TTL in the data available here.
    if (status == Network::DnsResolver::ResolutionStatus::Success) {
      ctx.resolved_hosts.reserve(response.size());
      for (const auto& resp : response) {
        ASSERT(resp.address_ != nullptr);
        ENVOY_LOG(trace, "Resolved address: {} for {}", resp.address_->ip()->addressAsString(),
                  ctx.query_rec->name_);
        ctx.resolved_hosts.emplace_back(std::move(resp.address_));
      }
    }
    // Invoke the filter callback notifying it of resolved addresses
    invokeCallback(ctx);
  };

  // Resolve the address in the query and add to the resolved_hosts vector
  active_query_ = resolver_->resolve(domain_query->name_, lookup_family, resolve_cb);
}

void DnsFilterResolver::onResolveTimeout() {
  const uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                           dispatcher_.timeSource().systemTime().time_since_epoch())
                           .count();
  ENVOY_LOG(trace, "Pending queries: {}", lookups_.size());

  // Find an outstanding pending query and purge it
  for (auto& ctx_iter : lookups_) {
    if (ctx_iter.second.expiry <= now &&
        ctx_iter.second.resolver_status == DnsFilterResolverStatus::Pending) {
      auto ctx = std::move(ctx_iter.second);

      ENVOY_LOG(trace, "Purging expired query: {}", ctx_iter.first);

      ctx.query_context->resolution_status_ = Network::DnsResolver::ResolutionStatus::Failure;

      lookups_.erase(ctx_iter.first);
      callback_(std::move(ctx.query_context), ctx.query_rec, ctx.resolved_hosts);
      return;
    }
  }
}
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

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
  external_context_ = std::move(context);

  // Because the context can have more than one query, we need to maintain a pointer to the current
  // query that is being resolved. Since domain_query is a unique pointer to a record in the
  // context, we use a standard pointer to reference the query data when building the response
  query_rec_ = domain_query;

  resolution_status_ = DnsFilterResolverStatus::Pending;
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
    ENVOY_LOG(debug, "Unknown query type [{}] for upstream lookup", domain_query->type_);
    invokeCallback(Network::DnsResolver::ResolutionStatus::Failure);
    return;
  }

  // Re-arm the timeout timer
  timeout_timer_->enableTimer(timeout_);

  // Define the callback that is executed when resolution completes
  auto resolve_cb = [this](Network::DnsResolver::ResolutionStatus status,
                           std::list<Network::DnsResponse>&& response) -> void {
    active_query_ = nullptr;
    ENVOY_LOG(trace, "async query status returned. Entries {}", response.size());
    if (resolution_status_ != DnsFilterResolverStatus::Pending) {
      ENVOY_LOG(debug, "Resolution timed out before callback was executed");
      return;
    }
    resolution_status_ = DnsFilterResolverStatus::Complete;
    // We are processing the response here, so we did not timeout. Cancel the timer
    timeout_timer_->disableTimer();

    // C-ares doesn't expose the TTL in the data available here.
    if (status == Network::DnsResolver::ResolutionStatus::Success) {
      for (const auto& resp : response) {
        ASSERT(resp.address_ != nullptr);
        ENVOY_LOG(trace, "Resolved address: {} for {}", resp.address_->ip()->addressAsString(),
                  query_rec_->name_);
        resolved_hosts_.push_back(std::move(resp.address_));
      }
    }
    // Invoke the filter callback notifying it of resolved addresses
    invokeCallback(status);
  };

  // Resolve the address in the query and add to the resolved_hosts vector
  resolved_hosts_.clear();
  active_query_ = resolver_->resolve(domain_query->name_, lookup_family, resolve_cb);
}
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

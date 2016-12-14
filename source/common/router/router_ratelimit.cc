#include "router_ratelimit.h"

namespace Router {

const std::vector<std::reference_wrapper<RateLimitPolicyEntry>>
    RateLimitPolicyImpl::empty_rate_limit_ = {};

void ServiceToServiceAction::populateDescriptors(const Router::RouteEntry& route,
                                                 std::vector<::RateLimit::Descriptor>& descriptors,
                                                 const std::string& local_service_cluster,
                                                 const Http::HeaderMap&,
                                                 Http::StreamDecoderFilterCallbacks&) const {
  // We limit on 2 dimensions.
  // 1) All calls to the given cluster.
  // 2) Calls to the given cluster and from this cluster.
  // The service side configuration can choose to limit on 1 or both of the above.
  descriptors.push_back({{{"to_cluster", route.clusterName()}}});
  descriptors.push_back(
      {{{"to_cluster", route.clusterName()}, {"from_cluster", local_service_cluster}}});
}

void RequestHeadersAction::populateDescriptors(const Router::RouteEntry& route,
                                               std::vector<::RateLimit::Descriptor>& descriptors,
                                               const std::string&, const Http::HeaderMap& headers,
                                               Http::StreamDecoderFilterCallbacks&) const {
  const Http::HeaderEntry* header_value = headers.get(header_name_);
  if (!header_value) {
    return;
  }

  descriptors.push_back({{{descriptor_key_, header_value->value().c_str()}}});

  const std::string& route_key = route.rateLimitPolicy().routeKey();
  if (route_key.empty()) {
    return;
  }

  descriptors.push_back(
      {{{"route_key", route_key}, {descriptor_key_, header_value->value().c_str()}}});
}

void RemoteAddressAction::populateDescriptors(const Router::RouteEntry& route,
                                              std::vector<::RateLimit::Descriptor>& descriptors,
                                              const std::string&, const Http::HeaderMap&,
                                              Http::StreamDecoderFilterCallbacks& callbacks) const {
  const std::string& remote_address = callbacks.downstreamAddress();
  if (remote_address.empty()) {
    return;
  }

  descriptors.push_back({{{"remote_address", remote_address}}});
  const std::string& route_key = route.rateLimitPolicy().routeKey();
  if (route_key.empty()) {
    return;
  }

  descriptors.push_back({{{"route_key", route_key}, {"remote_address", remote_address}}});
}

RateLimitPolicyEntryImpl::RateLimitPolicyEntryImpl(const Json::Object& config)
    : kill_switch_key_(config.getString("kill_switch_key", "")),
      stage_(config.getInteger("stage", 0)) {
  for (const Json::ObjectPtr& action : config.getObjectArray("actions")) {
    std::string type = action->getString("type");
    if (type == "service_to_service") {
      actions_.emplace_back(new ServiceToServiceAction());
    } else if (type == "request_headers") {
      actions_.emplace_back(new RequestHeadersAction(*action));
    } else if (type == "remote_address") {
      actions_.emplace_back(new RemoteAddressAction());
    } else {
      throw EnvoyException(fmt::format("unknown http rate limit filter action '{}'", type));
    }
  }
}

void RateLimitPolicyEntryImpl::populateDescriptors(
    const Router::RouteEntry& route, std::vector<::RateLimit::Descriptor>& descriptors,
    const std::string& local_service_cluster, const Http::HeaderMap& headers,
    Http::StreamDecoderFilterCallbacks& callbacks) const {
  for (const ActionPtr& action : actions_) {
    action->populateDescriptors(route, descriptors, local_service_cluster, headers, callbacks);
  }
}

RateLimitPolicyImpl::RateLimitPolicyImpl(const Json::Object& config)
    : route_key_(config.getObject("rate_limit", true)->getString("route_key", "")) {
  if (config.hasObject("rate_limits")) {
    std::vector<std::reference_wrapper<RateLimitPolicyEntry>> rate_limit_policy;
    for (const Json::ObjectPtr& rate_limit : config.getObjectArray("rate_limits")) {
      RateLimitPolicyEntryImpl* rate_limit_policy_entry = new RateLimitPolicyEntryImpl(*rate_limit);
      rate_limit_policy.emplace_back(*rate_limit_policy_entry);
    }
    rate_limit_entries_.push_back(rate_limit_policy);
  }
}

const std::vector<std::reference_wrapper<RateLimitPolicyEntry>>&
    RateLimitPolicyImpl::getApplicableRateLimit(int64_t) const {
  // Currently return all rate limit policy entries.
  // TODO: Implement returning only rate limit policy entries that match the stage setting.
  if (rate_limit_entries_.empty()) {
    return empty_rate_limit_;
  } else {
    return rate_limit_entries_[0];
  }
}

} // Router
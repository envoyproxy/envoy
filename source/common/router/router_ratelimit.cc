#include "router_ratelimit.h"

#include "common/common/empty_string.h"
#include "common/json/config_schemas.h"

namespace Router {

const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>
    RateLimitPolicyImpl::empty_rate_limit_;

void SourceClusterAction::populateDescriptor(const Router::RouteEntry&,
                                             ::RateLimit::Descriptor& descriptor,
                                             const std::string& local_service_cluster,
                                             const Http::HeaderMap&, const std::string&) const {
  descriptor.entries_.push_back({"source_cluster", local_service_cluster});
}

void DestinationClusterAction::populateDescriptor(const Router::RouteEntry& route,
                                                  ::RateLimit::Descriptor& descriptor,
                                                  const std::string&, const Http::HeaderMap&,
                                                  const std::string&) const {
  descriptor.entries_.push_back({"destination_cluster", route.clusterName()});
}

void RequestHeadersAction::populateDescriptor(const Router::RouteEntry&,
                                              ::RateLimit::Descriptor& descriptor,
                                              const std::string&, const Http::HeaderMap& headers,
                                              const std::string&) const {
  const Http::HeaderEntry* header_value = headers.get(header_name_);
  if (!header_value) {
    return;
  }

  descriptor.entries_.push_back({descriptor_key_, header_value->value().c_str()});
}

void RemoteAddressAction::populateDescriptor(const Router::RouteEntry&,
                                             ::RateLimit::Descriptor& descriptor,
                                             const std::string&, const Http::HeaderMap&,
                                             const std::string& remote_address) const {
  if (remote_address.empty()) {
    return;
  }

  descriptor.entries_.push_back({"remote_address", remote_address});
}

void GenericKeyAction::populateDescriptor(const Router::RouteEntry&,
                                          ::RateLimit::Descriptor& descriptor, const std::string&,
                                          const Http::HeaderMap&, const std::string&) const {
  descriptor.entries_.push_back({"generic_key", descriptor_value_});
}

HeaderValueMatchAction::HeaderValueMatchAction(const Json::Object& action)
    : descriptor_value_(action.getString("descriptor_value")) {
  std::vector<Json::ObjectPtr> config_headers = action.getObjectArray("headers");
  for (const Json::ObjectPtr& header_map : config_headers) {
    // allow header value to be empty, allows matching to be only based on header presence.
    // Regex is an opt-in. Unless explicitly mentioned, we will use header values for exact string
    // matches.
    action_headers_.emplace_back(Http::LowerCaseString(header_map->getString("name")),
                                 header_map->getString("value", EMPTY_STRING),
                                 header_map->getBoolean("regex", false));
  }
}

void HeaderValueMatchAction::populateDescriptor(const Router::RouteEntry&,
                                                ::RateLimit::Descriptor& descriptor,
                                                const std::string&, const Http::HeaderMap& headers,
                                                const std::string&) const {

  if (ConfigUtility::matchHeaders(headers, action_headers_)) {
    descriptor.entries_.push_back({"header_match", descriptor_value_});
  }
}

RateLimitPolicyEntryImpl::RateLimitPolicyEntryImpl(const Json::Object& config)
    : disable_key_(config.getString("disable_key", "")), stage_(config.getInteger("stage", 0)) {

  config.validateSchema(Json::Schema::HTTP_RATE_LIMITS_CONFIGURATION_SCHEMA);

  for (const Json::ObjectPtr& action : config.getObjectArray("actions")) {
    std::string type = action->getString("type");
    if (type == "source_cluster") {
      actions_.emplace_back(new SourceClusterAction());
    } else if (type == "destination_cluster") {
      actions_.emplace_back(new DestinationClusterAction());
    } else if (type == "request_headers") {
      actions_.emplace_back(new RequestHeadersAction(*action));
    } else if (type == "remote_address") {
      actions_.emplace_back(new RemoteAddressAction());
    } else if (type == "generic_key") {
      actions_.emplace_back(new GenericKeyAction(*action));
    } else if (type == "header_value_match") {
      actions_.emplace_back(new HeaderValueMatchAction(*action));
    } else {
      throw EnvoyException(fmt::format("unknown http rate limit filter action '{}'", type));
    }
  }
}

void RateLimitPolicyEntryImpl::populateDescriptors(
    const Router::RouteEntry& route, std::vector<::RateLimit::Descriptor>& descriptors,
    const std::string& local_service_cluster, const Http::HeaderMap& headers,
    const std::string& remote_address) const {
  ::RateLimit::Descriptor descriptor;
  for (const RateLimitActionPtr& action : actions_) {
    action->populateDescriptor(route, descriptor, local_service_cluster, headers, remote_address);
  }

  if (!descriptor.entries_.empty()) {
    descriptors.emplace_back(descriptor);
  }
}

RateLimitPolicyImpl::RateLimitPolicyImpl(const Json::Object& config) {
  if (config.hasObject("rate_limits")) {
    std::vector<std::unique_ptr<RateLimitPolicyEntry>> rate_limit_policy;
    std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limit_policy_reference;
    for (const Json::ObjectPtr& rate_limit : config.getObjectArray("rate_limits")) {
      std::unique_ptr<RateLimitPolicyEntry> rate_limit_policy_entry(
          new RateLimitPolicyEntryImpl(*rate_limit));
      rate_limit_policy_reference.emplace_back(*rate_limit_policy_entry);
      rate_limit_policy.emplace_back(std::move(rate_limit_policy_entry));
    }
    rate_limit_entries_.emplace_back(std::move(rate_limit_policy));
    rate_limit_entries_reference_.emplace_back(rate_limit_policy_reference);
  }
}

const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&
    RateLimitPolicyImpl::getApplicableRateLimit(int64_t) const {
  // Currently return all rate limit policy entries.
  // TODO: Implement returning only rate limit policy entries that match the stage setting.
  if (rate_limit_entries_.empty()) {
    return empty_rate_limit_;
  } else {
    return rate_limit_entries_reference_[0];
  }
}

} // Router
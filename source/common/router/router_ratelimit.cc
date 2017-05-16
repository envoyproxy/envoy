#include "common/router/router_ratelimit.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Router {

const uint64_t RateLimitPolicyImpl::MAX_STAGE_NUMBER = 10UL;

bool SourceClusterAction::populateDescriptor(const Router::RouteEntry&,
                                             RateLimit::Descriptor& descriptor,
                                             const std::string& local_service_cluster,
                                             const Http::HeaderMap&, const std::string&) const {
  descriptor.entries_.push_back({"source_cluster", local_service_cluster});
  return true;
}

bool DestinationClusterAction::populateDescriptor(const Router::RouteEntry& route,
                                                  RateLimit::Descriptor& descriptor,
                                                  const std::string&, const Http::HeaderMap&,
                                                  const std::string&) const {
  descriptor.entries_.push_back({"destination_cluster", route.clusterName()});
  return true;
}

bool RequestHeadersAction::populateDescriptor(const Router::RouteEntry&,
                                              RateLimit::Descriptor& descriptor, const std::string&,
                                              const Http::HeaderMap& headers,
                                              const std::string&) const {
  const Http::HeaderEntry* header_value = headers.get(header_name_);
  if (!header_value) {
    return false;
  }

  descriptor.entries_.push_back({descriptor_key_, header_value->value().c_str()});
  return true;
}

bool RemoteAddressAction::populateDescriptor(const Router::RouteEntry&,
                                             RateLimit::Descriptor& descriptor, const std::string&,
                                             const Http::HeaderMap&,
                                             const std::string& remote_address) const {
  if (remote_address.empty()) {
    return false;
  }

  descriptor.entries_.push_back({"remote_address", remote_address});
  return true;
}

bool GenericKeyAction::populateDescriptor(const Router::RouteEntry&,
                                          RateLimit::Descriptor& descriptor, const std::string&,
                                          const Http::HeaderMap&, const std::string&) const {
  descriptor.entries_.push_back({"generic_key", descriptor_value_});
  return true;
}

HeaderValueMatchAction::HeaderValueMatchAction(const Json::Object& action)
    : descriptor_value_(action.getString("descriptor_value")),
      expect_match_(action.getBoolean("expect_match", true)) {
  std::vector<Json::ObjectSharedPtr> config_headers = action.getObjectArray("headers");
  for (const Json::ObjectSharedPtr header_map : config_headers) {
    action_headers_.push_back(*header_map);
  }
}

bool HeaderValueMatchAction::populateDescriptor(const Router::RouteEntry&,
                                                RateLimit::Descriptor& descriptor,
                                                const std::string&, const Http::HeaderMap& headers,
                                                const std::string&) const {
  if (expect_match_ == ConfigUtility::matchHeaders(headers, action_headers_)) {
    descriptor.entries_.push_back({"header_match", descriptor_value_});
    return true;
  } else {
    return false;
  }
}

RateLimitPolicyEntryImpl::RateLimitPolicyEntryImpl(const Json::Object& config)
    : Json::Validator(config, Json::Schema::HTTP_RATE_LIMITS_CONFIGURATION_SCHEMA),
      disable_key_(config.getString("disable_key", "")),
      stage_(static_cast<uint64_t>(config.getInteger("stage", 0))) {
  for (const Json::ObjectSharedPtr action : config.getObjectArray("actions")) {
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
    } else {
      ASSERT(type == "header_value_match");
      actions_.emplace_back(new HeaderValueMatchAction(*action));
    }
  }
}

void RateLimitPolicyEntryImpl::populateDescriptors(const Router::RouteEntry& route,
                                                   std::vector<RateLimit::Descriptor>& descriptors,
                                                   const std::string& local_service_cluster,
                                                   const Http::HeaderMap& headers,
                                                   const std::string& remote_address) const {
  RateLimit::Descriptor descriptor;
  bool result = true;
  for (const RateLimitActionPtr& action : actions_) {
    result = result &&
             action->populateDescriptor(route, descriptor, local_service_cluster, headers,
                                        remote_address);
    if (!result) {
      break;
    }
  }

  if (result) {
    descriptors.emplace_back(descriptor);
  }
}

RateLimitPolicyImpl::RateLimitPolicyImpl(const Json::Object& config)
    : rate_limit_entries_reference_(RateLimitPolicyImpl::MAX_STAGE_NUMBER + 1) {
  if (config.hasObject("rate_limits")) {
    for (const Json::ObjectSharedPtr rate_limit : config.getObjectArray("rate_limits")) {
      std::unique_ptr<RateLimitPolicyEntry> rate_limit_policy_entry(
          new RateLimitPolicyEntryImpl(*rate_limit));
      uint64_t stage = rate_limit_policy_entry->stage();
      ASSERT(stage < rate_limit_entries_reference_.size());
      rate_limit_entries_reference_[stage].emplace_back(*rate_limit_policy_entry);
      rate_limit_entries_.emplace_back(std::move(rate_limit_policy_entry));
    }
  }
}

const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>&
RateLimitPolicyImpl::getApplicableRateLimit(uint64_t stage) const {
  ASSERT(stage < rate_limit_entries_reference_.size());
  return rate_limit_entries_reference_[stage];
}

} // Router
} // Envoy

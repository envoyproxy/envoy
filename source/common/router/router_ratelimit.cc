#include "common/router/router_ratelimit.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

const uint64_t RateLimitPolicyImpl::MAX_STAGE_NUMBER = 10UL;

bool SourceClusterAction::populateDescriptor(const Router::RouteEntry&,
                                             RateLimit::Descriptor& descriptor,
                                             const std::string& local_service_cluster,
                                             const Http::HeaderMap&,
                                             const Network::Address::Instance&) const {
  descriptor.entries_.push_back({"source_cluster", local_service_cluster});
  return true;
}

bool DestinationClusterAction::populateDescriptor(const Router::RouteEntry& route,
                                                  RateLimit::Descriptor& descriptor,
                                                  const std::string&, const Http::HeaderMap&,
                                                  const Network::Address::Instance&) const {
  descriptor.entries_.push_back({"destination_cluster", route.clusterName()});
  return true;
}

bool RequestHeadersAction::populateDescriptor(const Router::RouteEntry&,
                                              RateLimit::Descriptor& descriptor, const std::string&,
                                              const Http::HeaderMap& headers,
                                              const Network::Address::Instance&) const {
  const Http::HeaderEntry* header_value = headers.get(header_name_);
  if (!header_value) {
    return false;
  }

  descriptor.entries_.push_back({descriptor_key_, header_value->value().c_str()});
  return true;
}

bool RemoteAddressAction::populateDescriptor(
    const Router::RouteEntry&, RateLimit::Descriptor& descriptor, const std::string&,
    const Http::HeaderMap&, const Network::Address::Instance& remote_address) const {
  if (remote_address.type() != Network::Address::Type::Ip) {
    return false;
  }

  descriptor.entries_.push_back({"remote_address", remote_address.ip()->addressAsString()});
  return true;
}

bool GenericKeyAction::populateDescriptor(const Router::RouteEntry&,
                                          RateLimit::Descriptor& descriptor, const std::string&,
                                          const Http::HeaderMap&,
                                          const Network::Address::Instance&) const {
  descriptor.entries_.push_back({"generic_key", descriptor_value_});
  return true;
}

HeaderValueMatchAction::HeaderValueMatchAction(
    const envoy::api::v2::route::RateLimit::Action::HeaderValueMatch& action)
    : descriptor_value_(action.descriptor_value()),
      expect_match_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(action, expect_match, true)) {
  for (const auto& header_matcher : action.headers()) {
    action_headers_.push_back(header_matcher);
  }
}

bool HeaderValueMatchAction::populateDescriptor(const Router::RouteEntry&,
                                                RateLimit::Descriptor& descriptor,
                                                const std::string&, const Http::HeaderMap& headers,
                                                const Network::Address::Instance&) const {
  if (expect_match_ == Http::HeaderUtility::matchHeaders(headers, action_headers_)) {
    descriptor.entries_.push_back({"header_match", descriptor_value_});
    return true;
  } else {
    return false;
  }
}

RateLimitPolicyEntryImpl::RateLimitPolicyEntryImpl(const envoy::api::v2::route::RateLimit& config)
    : disable_key_(config.disable_key()),
      stage_(static_cast<uint64_t>(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, stage, 0))) {
  for (const auto& action : config.actions()) {
    switch (action.action_specifier_case()) {
    case envoy::api::v2::route::RateLimit::Action::kSourceCluster:
      actions_.emplace_back(new SourceClusterAction());
      break;
    case envoy::api::v2::route::RateLimit::Action::kDestinationCluster:
      actions_.emplace_back(new DestinationClusterAction());
      break;
    case envoy::api::v2::route::RateLimit::Action::kRequestHeaders:
      actions_.emplace_back(new RequestHeadersAction(action.request_headers()));
      break;
    case envoy::api::v2::route::RateLimit::Action::kRemoteAddress:
      actions_.emplace_back(new RemoteAddressAction());
      break;
    case envoy::api::v2::route::RateLimit::Action::kGenericKey:
      actions_.emplace_back(new GenericKeyAction(action.generic_key()));
      break;
    case envoy::api::v2::route::RateLimit::Action::kHeaderValueMatch:
      actions_.emplace_back(new HeaderValueMatchAction(action.header_value_match()));
      break;
    default:
      NOT_REACHED;
    }
  }
}

void RateLimitPolicyEntryImpl::populateDescriptors(
    const Router::RouteEntry& route, std::vector<RateLimit::Descriptor>& descriptors,
    const std::string& local_service_cluster, const Http::HeaderMap& headers,
    const Network::Address::Instance& remote_address) const {
  RateLimit::Descriptor descriptor;
  bool result = true;
  for (const RateLimitActionPtr& action : actions_) {
    result = result && action->populateDescriptor(route, descriptor, local_service_cluster, headers,
                                                  remote_address);
    if (!result) {
      break;
    }
  }

  if (result) {
    descriptors.emplace_back(descriptor);
  }
}

RateLimitPolicyImpl::RateLimitPolicyImpl(
    const Protobuf::RepeatedPtrField<envoy::api::v2::route::RateLimit>& rate_limits)
    : rate_limit_entries_reference_(RateLimitPolicyImpl::MAX_STAGE_NUMBER + 1) {
  for (const auto& rate_limit : rate_limits) {
    std::unique_ptr<RateLimitPolicyEntry> rate_limit_policy_entry(
        new RateLimitPolicyEntryImpl(rate_limit));
    uint64_t stage = rate_limit_policy_entry->stage();
    ASSERT(stage < rate_limit_entries_reference_.size());
    rate_limit_entries_reference_[stage].emplace_back(*rate_limit_policy_entry);
    rate_limit_entries_.emplace_back(std::move(rate_limit_policy_entry));
  }
}

const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>&
RateLimitPolicyImpl::getApplicableRateLimit(uint64_t stage) const {
  ASSERT(stage < rate_limit_entries_reference_.size());
  return rate_limit_entries_reference_[stage];
}

} // namespace Router
} // namespace Envoy

#include "extensions/filters/network/thrift_proxy/router/router_ratelimit_impl.h"

#include "envoy/common/exception.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/ratelimit/ratelimit.h"

#include "extensions/filters/network/thrift_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

bool SourceClusterAction::populateDescriptor(const RouteEntry&, RateLimit::Descriptor& descriptor,
                                             const std::string& local_service_cluster,
                                             const MessageMetadata&,
                                             const Network::Address::Instance&) const {
  descriptor.entries_.push_back({"source_cluster", local_service_cluster});
  return true;
}

bool DestinationClusterAction::populateDescriptor(const RouteEntry& route,
                                                  RateLimit::Descriptor& descriptor,
                                                  const std::string&, const MessageMetadata&,
                                                  const Network::Address::Instance&) const {
  descriptor.entries_.push_back({"destination_cluster", route.clusterName()});
  return true;
}

bool RequestHeadersAction::populateDescriptor(const RouteEntry&, RateLimit::Descriptor& descriptor,
                                              const std::string&, const MessageMetadata& metadata,
                                              const Network::Address::Instance&) const {
  if (use_method_name_) {
    if (!metadata.hasMethodName()) {
      return false;
    }

    descriptor.entries_.push_back({descriptor_key_, metadata.methodName()});
    return true;
  }

  const Http::HeaderEntry* header_value = metadata.headers().get(header_name_);
  if (!header_value) {
    return false;
  }

  descriptor.entries_.push_back(
      {descriptor_key_, std::string(header_value->value().getStringView())});
  return true;
}

bool RemoteAddressAction::populateDescriptor(
    const RouteEntry&, RateLimit::Descriptor& descriptor, const std::string&,
    const MessageMetadata&, const Network::Address::Instance& remote_address) const {
  if (remote_address.type() != Network::Address::Type::Ip) {
    return false;
  }

  descriptor.entries_.push_back({"remote_address", remote_address.ip()->addressAsString()});
  return true;
}

bool GenericKeyAction::populateDescriptor(const RouteEntry&, RateLimit::Descriptor& descriptor,
                                          const std::string&, const MessageMetadata&,
                                          const Network::Address::Instance&) const {
  descriptor.entries_.push_back({"generic_key", descriptor_value_});
  return true;
}

HeaderValueMatchAction::HeaderValueMatchAction(
    const envoy::config::route::v3::RateLimit::Action::HeaderValueMatch& action)
    : descriptor_value_(action.descriptor_value()),
      expect_match_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(action, expect_match, true)),
      action_headers_(Http::HeaderUtility::buildHeaderDataVector(action.headers())) {}

bool HeaderValueMatchAction::populateDescriptor(const RouteEntry&,
                                                RateLimit::Descriptor& descriptor,
                                                const std::string&, const MessageMetadata& metadata,
                                                const Network::Address::Instance&) const {
  if (expect_match_ == Http::HeaderUtility::matchHeaders(metadata.headers(), action_headers_)) {
    descriptor.entries_.push_back({"header_match", descriptor_value_});
    return true;
  } else {
    return false;
  }
}

RateLimitPolicyEntryImpl::RateLimitPolicyEntryImpl(
    const envoy::config::route::v3::RateLimit& config)
    : disable_key_(config.disable_key()),
      stage_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, stage, 0)) {
  for (const auto& action : config.actions()) {
    switch (action.action_specifier_case()) {
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kSourceCluster:
      actions_.emplace_back(new SourceClusterAction());
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kDestinationCluster:
      actions_.emplace_back(new DestinationClusterAction());
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kRequestHeaders:
      actions_.emplace_back(new RequestHeadersAction(action.request_headers()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kRemoteAddress:
      actions_.emplace_back(new RemoteAddressAction());
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kGenericKey:
      actions_.emplace_back(new GenericKeyAction(action.generic_key()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kHeaderValueMatch:
      actions_.emplace_back(new HeaderValueMatchAction(action.header_value_match()));
      break;
    default:
      throw EnvoyException(
          absl::StrCat("unsupported RateLimit Action ", action.action_specifier_case()));
    }
  }
}

void RateLimitPolicyEntryImpl::populateDescriptors(
    const RouteEntry& route, std::vector<RateLimit::Descriptor>& descriptors,
    const std::string& local_service_cluster, const MessageMetadata& metadata,
    const Network::Address::Instance& remote_address) const {
  RateLimit::Descriptor descriptor;
  bool result = true;
  for (const RateLimitActionPtr& action : actions_) {
    result = result && action->populateDescriptor(route, descriptor, local_service_cluster,
                                                  metadata, remote_address);
    if (!result) {
      break;
    }
  }

  if (result) {
    descriptors.emplace_back(descriptor);
  }
}

RateLimitPolicyImpl::RateLimitPolicyImpl(
    const Protobuf::RepeatedPtrField<envoy::config::route::v3::RateLimit>& rate_limits)
    : rate_limit_entries_reference_(RateLimitPolicyImpl::MAX_STAGE_NUMBER + 1) {
  for (const auto& rate_limit : rate_limits) {
    std::unique_ptr<RateLimitPolicyEntry> rate_limit_policy_entry(
        new RateLimitPolicyEntryImpl(rate_limit));
    uint32_t stage = rate_limit_policy_entry->stage();
    ASSERT(stage < rate_limit_entries_reference_.size());
    rate_limit_entries_reference_[stage].emplace_back(*rate_limit_policy_entry);
    rate_limit_entries_.emplace_back(std::move(rate_limit_policy_entry));
  }
}

const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>&
RateLimitPolicyImpl::getApplicableRateLimit(uint32_t stage) const {
  ASSERT(stage < rate_limit_entries_reference_.size());
  return rate_limit_entries_reference_[stage];
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

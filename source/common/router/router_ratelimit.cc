#include "common/router/router_ratelimit.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/config/metadata.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

const uint64_t RateLimitPolicyImpl::MAX_STAGE_NUMBER = 10UL;

bool DynamicMetadataRateLimitOverride::populateOverride(
    RateLimit::Descriptor& descriptor, const envoy::config::core::v3::Metadata* metadata) const {
  const ProtobufWkt::Value& metadata_value =
      Envoy::Config::Metadata::metadataValue(metadata, metadata_key_);
  if (metadata_value.kind_case() != ProtobufWkt::Value::kStructValue) {
    return false;
  }

  const auto& override_value = metadata_value.struct_value().fields();
  const auto& limit_it = override_value.find("requests_per_unit");
  const auto& unit_it = override_value.find("unit");
  if (limit_it != override_value.end() &&
      limit_it->second.kind_case() == ProtobufWkt::Value::kNumberValue &&
      unit_it != override_value.end() &&
      unit_it->second.kind_case() == ProtobufWkt::Value::kStringValue) {
    envoy::type::v3::RateLimitUnit unit;
    if (envoy::type::v3::RateLimitUnit_Parse(unit_it->second.string_value(), &unit)) {
      descriptor.limit_.emplace(RateLimit::RateLimitOverride{
          static_cast<uint32_t>(limit_it->second.number_value()), unit});
      return true;
    }
  }
  return false;
}

bool SourceClusterAction::populateDescriptor(const Router::RouteEntry&,
                                             RateLimit::Descriptor& descriptor,
                                             const std::string& local_service_cluster,
                                             const Http::HeaderMap&,
                                             const Network::Address::Instance&,
                                             const envoy::config::core::v3::Metadata*) const {
  descriptor.entries_.push_back({"source_cluster", local_service_cluster});
  return true;
}

bool DestinationClusterAction::populateDescriptor(const Router::RouteEntry& route,
                                                  RateLimit::Descriptor& descriptor,
                                                  const std::string&, const Http::HeaderMap&,
                                                  const Network::Address::Instance&,
                                                  const envoy::config::core::v3::Metadata*) const {
  descriptor.entries_.push_back({"destination_cluster", route.clusterName()});
  return true;
}

bool RequestHeadersAction::populateDescriptor(const Router::RouteEntry&,
                                              RateLimit::Descriptor& descriptor, const std::string&,
                                              const Http::HeaderMap& headers,
                                              const Network::Address::Instance&,
                                              const envoy::config::core::v3::Metadata*) const {
  const Http::HeaderEntry* header_value = headers.get(header_name_);

  // If header is not present in the request and if skip_if_absent is true skip this descriptor,
  // while calling rate limiting service. If skip_if_absent is false, do not call rate limiting
  // service.
  if (!header_value) {
    return skip_if_absent_;
  }
  descriptor.entries_.push_back(
      {descriptor_key_, std::string(header_value->value().getStringView())});
  return true;
}

bool RemoteAddressAction::populateDescriptor(const Router::RouteEntry&,
                                             RateLimit::Descriptor& descriptor, const std::string&,
                                             const Http::HeaderMap&,
                                             const Network::Address::Instance& remote_address,
                                             const envoy::config::core::v3::Metadata*) const {
  if (remote_address.type() != Network::Address::Type::Ip) {
    return false;
  }

  descriptor.entries_.push_back({"remote_address", remote_address.ip()->addressAsString()});
  return true;
}

bool GenericKeyAction::populateDescriptor(const Router::RouteEntry&,
                                          RateLimit::Descriptor& descriptor, const std::string&,
                                          const Http::HeaderMap&, const Network::Address::Instance&,
                                          const envoy::config::core::v3::Metadata*) const {
  descriptor.entries_.push_back({"generic_key", descriptor_value_});
  return true;
}

DynamicMetaDataAction::DynamicMetaDataAction(
    const envoy::config::route::v3::RateLimit::Action::DynamicMetaData& action)
    : metadata_key_(action.metadata_key()), descriptor_key_(action.descriptor_key()),
      default_value_(action.default_value()) {}

bool DynamicMetaDataAction::populateDescriptor(
    const Router::RouteEntry&, RateLimit::Descriptor& descriptor, const std::string&,
    const Http::HeaderMap&, const Network::Address::Instance&,
    const envoy::config::core::v3::Metadata* dynamic_metadata) const {
  const ProtobufWkt::Value& metadata_value =
      Envoy::Config::Metadata::metadataValue(dynamic_metadata, metadata_key_);

  if (!metadata_value.string_value().empty()) {
    descriptor.entries_.push_back({descriptor_key_, metadata_value.string_value()});
    return true;
  } else if (metadata_value.string_value().empty() && !default_value_.empty()) {
    descriptor.entries_.push_back({descriptor_key_, default_value_});
    return true;
  }

  return false;
}

HeaderValueMatchAction::HeaderValueMatchAction(
    const envoy::config::route::v3::RateLimit::Action::HeaderValueMatch& action)
    : descriptor_value_(action.descriptor_value()),
      expect_match_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(action, expect_match, true)),
      action_headers_(Http::HeaderUtility::buildHeaderDataVector(action.headers())) {}

bool HeaderValueMatchAction::populateDescriptor(const Router::RouteEntry&,
                                                RateLimit::Descriptor& descriptor,
                                                const std::string&, const Http::HeaderMap& headers,
                                                const Network::Address::Instance&,
                                                const envoy::config::core::v3::Metadata*) const {
  if (expect_match_ == Http::HeaderUtility::matchHeaders(headers, action_headers_)) {
    descriptor.entries_.push_back({"header_match", descriptor_value_});
    return true;
  } else {
    return false;
  }
}

RateLimitPolicyEntryImpl::RateLimitPolicyEntryImpl(
    const envoy::config::route::v3::RateLimit& config)
    : disable_key_(config.disable_key()),
      stage_(static_cast<uint64_t>(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, stage, 0))) {
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
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kDynamicMetadata:
      actions_.emplace_back(new DynamicMetaDataAction(action.dynamic_metadata()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kHeaderValueMatch:
      actions_.emplace_back(new HeaderValueMatchAction(action.header_value_match()));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  if (config.has_limit()) {
    switch (config.limit().override_specifier_case()) {
    case envoy::config::route::v3::RateLimit_Override::OverrideSpecifierCase::kDynamicMetadata:
      limit_override_.emplace(
          new DynamicMetadataRateLimitOverride(config.limit().dynamic_metadata()));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
}

void RateLimitPolicyEntryImpl::populateDescriptors(
    const Router::RouteEntry& route, std::vector<RateLimit::Descriptor>& descriptors,
    const std::string& local_service_cluster, const Http::HeaderMap& headers,
    const Network::Address::Instance& remote_address,
    const envoy::config::core::v3::Metadata* dynamic_metadata) const {
  RateLimit::Descriptor descriptor;
  bool result = true;
  for (const RateLimitActionPtr& action : actions_) {
    result = result && action->populateDescriptor(route, descriptor, local_service_cluster, headers,
                                                  remote_address, dynamic_metadata);
    if (!result) {
      break;
    }
  }

  if (limit_override_) {
    limit_override_.value()->populateOverride(descriptor, dynamic_metadata);
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

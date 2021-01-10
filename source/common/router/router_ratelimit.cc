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
#include "common/config/utility.h"
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

bool SourceClusterAction::populateDescriptor(RateLimit::Descriptor& descriptor,
                                             const std::string& local_service_cluster,
                                             const Http::RequestHeaderMap&,
                                             const StreamInfo::StreamInfo&) const {
  descriptor.entries_.push_back({"source_cluster", local_service_cluster});
  return true;
}

bool DestinationClusterAction::populateDescriptor(RateLimit::Descriptor& descriptor,
                                                  const std::string&, const Http::RequestHeaderMap&,
                                                  const StreamInfo::StreamInfo& info) const {
  descriptor.entries_.push_back({"destination_cluster", info.routeEntry()->clusterName()});
  return true;
}

bool RequestHeadersAction::populateDescriptor(RateLimit::Descriptor& descriptor, const std::string&,
                                              const Http::RequestHeaderMap& headers,
                                              const StreamInfo::StreamInfo&) const {
  const auto header_value = headers.get(header_name_);

  // If header is not present in the request and if skip_if_absent is true skip this descriptor,
  // while calling rate limiting service. If skip_if_absent is false, do not call rate limiting
  // service.
  if (header_value.empty()) {
    return skip_if_absent_;
  }
  // TODO(https://github.com/envoyproxy/envoy/issues/13454): Potentially populate all header values.
  descriptor.entries_.push_back(
      {descriptor_key_, std::string(header_value[0]->value().getStringView())});
  return true;
}

bool RemoteAddressAction::populateDescriptor(RateLimit::Descriptor& descriptor, const std::string&,
                                             const Http::RequestHeaderMap&,
                                             const StreamInfo::StreamInfo& info) const {
  const Network::Address::InstanceConstSharedPtr& remote_address =
      info.downstreamAddressProvider().remoteAddress();
  if (remote_address->type() != Network::Address::Type::Ip) {
    return false;
  }

  descriptor.entries_.push_back({"remote_address", remote_address->ip()->addressAsString()});
  return true;
}

bool GenericKeyAction::populateDescriptor(RateLimit::Descriptor& descriptor, const std::string&,
                                          const Http::RequestHeaderMap&,
                                          const StreamInfo::StreamInfo&) const {
  descriptor.entries_.push_back({descriptor_key_, descriptor_value_});
  return true;
}

MetaDataAction::MetaDataAction(const envoy::config::route::v3::RateLimit::Action::MetaData& action)
    : metadata_key_(action.metadata_key()), descriptor_key_(action.descriptor_key()),
      default_value_(action.default_value()), source_(action.source()) {}

MetaDataAction::MetaDataAction(
    const envoy::config::route::v3::RateLimit::Action::DynamicMetaData& action)
    : metadata_key_(action.metadata_key()), descriptor_key_(action.descriptor_key()),
      default_value_(action.default_value()),
      source_(envoy::config::route::v3::RateLimit::Action::MetaData::DYNAMIC) {}

bool MetaDataAction::populateDescriptor(RateLimit::Descriptor& descriptor, const std::string&,
                                        const Http::RequestHeaderMap&,
                                        const StreamInfo::StreamInfo& info) const {
  const envoy::config::core::v3::Metadata* metadata_source;

  switch (source_) {
  case envoy::config::route::v3::RateLimit::Action::MetaData::DYNAMIC:
    metadata_source = &info.dynamicMetadata();
    break;
  case envoy::config::route::v3::RateLimit::Action::MetaData::ROUTE_ENTRY:
    metadata_source = &info.routeEntry()->metadata();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const std::string metadata_string_value =
      Envoy::Config::Metadata::metadataValue(metadata_source, metadata_key_).string_value();

  if (!metadata_string_value.empty()) {
    descriptor.entries_.push_back({descriptor_key_, metadata_string_value});
    return true;
  } else if (metadata_string_value.empty() && !default_value_.empty()) {
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

bool HeaderValueMatchAction::populateDescriptor(RateLimit::Descriptor& descriptor,
                                                const std::string&,
                                                const Http::RequestHeaderMap& headers,
                                                const StreamInfo::StreamInfo&) const {
  if (expect_match_ == Http::HeaderUtility::matchHeaders(headers, action_headers_)) {
    descriptor.entries_.push_back({"header_match", descriptor_value_});
    return true;
  } else {
    return false;
  }
}

RateLimitPolicyEntryImpl::RateLimitPolicyEntryImpl(
    const envoy::config::route::v3::RateLimit& config,
    ProtobufMessage::ValidationVisitor& validator)
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
      actions_.emplace_back(new MetaDataAction(action.dynamic_metadata()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kMetadata:
      actions_.emplace_back(new MetaDataAction(action.metadata()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kHeaderValueMatch:
      actions_.emplace_back(new HeaderValueMatchAction(action.header_value_match()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kExtension: {
      auto* factory = Envoy::Config::Utility::getFactory<RateLimit::DescriptorProducerFactory>(
          action.extension());
      if (!factory) {
        throw EnvoyException(
            absl::StrCat("Rate limit descriptor extension not found: ", action.extension().name()));
      }
      auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
          action.extension().typed_config(), validator, *factory);
      RateLimit::DescriptorProducerPtr producer =
          factory->createDescriptorProducerFromProto(*message, validator);
      if (producer) {
        actions_.emplace_back(std::move(producer));
      } else {
        throw EnvoyException(
            absl::StrCat("Rate limit descriptor extension failed: ", action.extension().name()));
      }
      break;
    }
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

void RateLimitPolicyEntryImpl::populateDescriptors(std::vector<RateLimit::Descriptor>& descriptors,
                                                   const std::string& local_service_cluster,
                                                   const Http::RequestHeaderMap& headers,
                                                   const StreamInfo::StreamInfo& info) const {
  RateLimit::Descriptor descriptor;
  bool result = true;
  for (const RateLimit::DescriptorProducerPtr& action : actions_) {
    result = result && action->populateDescriptor(descriptor, local_service_cluster, headers, info);
    if (!result) {
      break;
    }
  }

  if (limit_override_) {
    limit_override_.value()->populateOverride(descriptor, &info.dynamicMetadata());
  }

  if (result) {
    descriptors.emplace_back(descriptor);
  }
}

RateLimitPolicyImpl::RateLimitPolicyImpl(
    const Protobuf::RepeatedPtrField<envoy::config::route::v3::RateLimit>& rate_limits,
    ProtobufMessage::ValidationVisitor& validator)
    : rate_limit_entries_reference_(RateLimitPolicyImpl::MAX_STAGE_NUMBER + 1) {
  for (const auto& rate_limit : rate_limits) {
    std::unique_ptr<RateLimitPolicyEntry> rate_limit_policy_entry(
        new RateLimitPolicyEntryImpl(rate_limit, validator));
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

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"

#include "source/common/config/metadata.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"
#include "source/common/network/cidr_range.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_utility.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

/**
 * Populate rate limit override from dynamic metadata.
 */
class DynamicMetadataRateLimitOverride : public RateLimitOverrideAction {
public:
  DynamicMetadataRateLimitOverride(
      const envoy::config::route::v3::RateLimit::Override::DynamicMetadata& config)
      : metadata_key_(config.metadata_key()) {}

  // Router::RateLimitOverrideAction
  bool populateOverride(RateLimit::Descriptor& descriptor,
                        const envoy::config::core::v3::Metadata* metadata) const override;

private:
  const Envoy::Config::MetadataKey metadata_key_;
};

/**
 * Action for source cluster rate limiting.
 */
class SourceClusterAction : public RateLimit::DescriptorProducer {
public:
  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                          const std::string& local_service_cluster,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;
};

/**
 * Action for destination cluster rate limiting.
 */
class DestinationClusterAction : public RateLimit::DescriptorProducer {
public:
  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                          const std::string& local_service_cluster,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;
};

/**
 * Action for request headers rate limiting.
 */
class RequestHeadersAction : public RateLimit::DescriptorProducer {
public:
  RequestHeadersAction(const envoy::config::route::v3::RateLimit::Action::RequestHeaders& action)
      : header_name_(action.header_name()), descriptor_key_(action.descriptor_key()),
        skip_if_absent_(action.skip_if_absent()) {}

  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                          const std::string& local_service_cluster,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;

private:
  const Http::LowerCaseString header_name_;
  const std::string descriptor_key_;
  const bool skip_if_absent_;
};

/**
 * Action for remote address rate limiting.
 */
class RemoteAddressAction : public RateLimit::DescriptorProducer {
public:
  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                          const std::string& local_service_cluster,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;
};

/**
 * Action for masked remote address rate limiting.
 */
class MaskedRemoteAddressAction : public RateLimit::DescriptorProducer {
public:
  MaskedRemoteAddressAction(
      const envoy::config::route::v3::RateLimit::Action::MaskedRemoteAddress& action)
      : v4_prefix_mask_len_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(action, v4_prefix_mask_len, 32)),
        v6_prefix_mask_len_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(action, v6_prefix_mask_len, 128)) {}

  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                          const std::string& local_service_cluster,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;

private:
  const uint32_t v4_prefix_mask_len_;
  const uint32_t v6_prefix_mask_len_;
};

/**
 * Action for generic key rate limiting.
 */
class GenericKeyAction : public RateLimit::DescriptorProducer {
public:
  GenericKeyAction(const envoy::config::route::v3::RateLimit::Action::GenericKey& action)
      : descriptor_value_(action.descriptor_value()),
        descriptor_key_(!action.descriptor_key().empty() ? action.descriptor_key()
                                                         : "generic_key") {}

  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                          const std::string& local_service_cluster,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;

private:
  const std::string descriptor_value_;
  const std::string descriptor_key_;
};

/**
 * Action for metadata rate limiting.
 */
class MetaDataAction : public RateLimit::DescriptorProducer {
public:
  MetaDataAction(const envoy::config::route::v3::RateLimit::Action::MetaData& action);
  // for maintaining backward compatibility with the deprecated DynamicMetaData action
  MetaDataAction(const envoy::config::route::v3::RateLimit::Action::DynamicMetaData& action);

  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                          const std::string& local_service_cluster,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;

private:
  const Envoy::Config::MetadataKey metadata_key_;
  const std::string descriptor_key_;
  const std::string default_value_;
  const envoy::config::route::v3::RateLimit::Action::MetaData::Source source_;
  const bool skip_if_absent_;
};

/**
 * Action for header value match rate limiting.
 */
class HeaderValueMatchAction : public RateLimit::DescriptorProducer {
public:
  HeaderValueMatchAction(
      const envoy::config::route::v3::RateLimit::Action::HeaderValueMatch& action,
      Server::Configuration::CommonFactoryContext& context);

  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                          const std::string& local_service_cluster,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;

private:
  const std::string descriptor_value_;
  const std::string descriptor_key_;
  const bool expect_match_;
  const std::vector<Http::HeaderUtility::HeaderDataPtr> action_headers_;
};

/**
 * Action for query parameter value match rate limiting.
 */
class QueryParameterValueMatchAction : public RateLimit::DescriptorProducer {
public:
  QueryParameterValueMatchAction(
      const envoy::config::route::v3::RateLimit::Action::QueryParameterValueMatch& action,
      Server::Configuration::CommonFactoryContext& context);

  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                          const std::string& local_service_cluster,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;

  std::vector<ConfigUtility::QueryParameterMatcherPtr> buildQueryParameterMatcherVector(
      const Protobuf::RepeatedPtrField<envoy::config::route::v3::QueryParameterMatcher>&
          query_parameters,
      Server::Configuration::CommonFactoryContext& context);

private:
  const std::string descriptor_value_;
  const std::string descriptor_key_;
  const bool expect_match_;
  const std::vector<ConfigUtility::QueryParameterMatcherPtr> action_query_parameters_;
};

class RateLimitDescriptorValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

class MatchInputRateLimitDescriptor : public RateLimit::DescriptorProducer {
public:
  MatchInputRateLimitDescriptor(const std::string& descriptor_key,
                                Matcher::DataInputPtr<Http::HttpMatchingData>&& data_input)
      : descriptor_key_(descriptor_key), data_input_(std::move(data_input)) {}

  // Ratelimit::DescriptorProducer
  bool populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry, const std::string&,
                          const Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo& info) const override;

private:
  const std::string descriptor_key_;
  Matcher::DataInputPtr<Http::HttpMatchingData> data_input_;
};

/*
 * Implementation of RateLimitPolicyEntry that holds the action for the configuration.
 */
class RateLimitPolicyEntryImpl : public RateLimitPolicyEntry {
public:
  RateLimitPolicyEntryImpl(const envoy::config::route::v3::RateLimit& config,
                           Server::Configuration::CommonFactoryContext& context,
                           absl::Status& creation_status);

  // Router::RateLimitPolicyEntry
  uint64_t stage() const override { return stage_; }
  const std::string& disableKey() const override { return disable_key_; }
  void populateDescriptors(std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                           const std::string& local_service_cluster, const Http::RequestHeaderMap&,
                           const StreamInfo::StreamInfo& info) const override;
  void populateLocalDescriptors(std::vector<Envoy::RateLimit::LocalDescriptor>& descriptors,
                                const std::string& local_service_cluster,
                                const Http::RequestHeaderMap&,
                                const StreamInfo::StreamInfo& info) const override;

private:
  const std::string disable_key_;
  uint64_t stage_;
  std::vector<RateLimit::DescriptorProducerPtr> actions_;
  absl::optional<RateLimitOverrideActionPtr> limit_override_ = absl::nullopt;
};

/**
 * Implementation of RateLimitPolicy that reads from the JSON route config.
 */
class RateLimitPolicyImpl : public RateLimitPolicy {
public:
  RateLimitPolicyImpl();
  RateLimitPolicyImpl(
      const Protobuf::RepeatedPtrField<envoy::config::route::v3::RateLimit>& rate_limits,
      Server::Configuration::CommonFactoryContext& context, absl::Status& creation_status);

  // Router::RateLimitPolicy
  const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&
  getApplicableRateLimit(uint64_t stage = 0) const override;
  bool empty() const override { return rate_limit_entries_.empty(); }

private:
  std::vector<std::unique_ptr<RateLimitPolicyEntry>> rate_limit_entries_;
  std::vector<std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>>
      rate_limit_entries_reference_;
  // The maximum stage number supported. This value should match the maximum stage number in
  // Json::Schema::HTTP_RATE_LIMITS_CONFIGURATION_SCHEMA and
  // Json::Schema::RATE_LIMIT_HTTP_FILTER_SCHEMA from common/json/config_schemas.cc.
  static const uint64_t MAX_STAGE_NUMBER;
};
using DefaultRateLimitPolicy = ConstSingleton<RateLimitPolicyImpl>;

} // namespace Router
} // namespace Envoy

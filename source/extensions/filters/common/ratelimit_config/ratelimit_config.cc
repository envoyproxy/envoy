#include "source/extensions/filters/common/ratelimit_config/ratelimit_config.h"
#include "ratelimit_config.h"
#include "source/common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

RateLimitPolicy::RateLimitPolicy(const ProtoRateLimit& config,
                                 Server::Configuration::CommonFactoryContext& context,
                                 absl::Status& creation_status) {
  for (const ProtoRateLimit::Action& action : config.actions()) {
    switch (action.action_specifier_case()) {
    case ProtoRateLimit::Action::ActionSpecifierCase::kSourceCluster:
      actions_.emplace_back(new Envoy::Router::SourceClusterAction());
      break;
    case ProtoRateLimit::Action::ActionSpecifierCase::kDestinationCluster:
      actions_.emplace_back(new Envoy::Router::DestinationClusterAction());
      break;
    case ProtoRateLimit::Action::ActionSpecifierCase::kRequestHeaders:
      actions_.emplace_back(new Envoy::Router::RequestHeadersAction(action.request_headers()));
      break;
    case ProtoRateLimit::Action::ActionSpecifierCase::kRemoteAddress:
      actions_.emplace_back(new Envoy::Router::RemoteAddressAction());
      break;
    case ProtoRateLimit::Action::ActionSpecifierCase::kGenericKey:
      actions_.emplace_back(new Envoy::Router::GenericKeyAction(action.generic_key()));
      break;
    case ProtoRateLimit::Action::ActionSpecifierCase::kMetadata:
      actions_.emplace_back(new Envoy::Router::MetaDataAction(action.metadata()));
      break;
    case ProtoRateLimit::Action::ActionSpecifierCase::kHeaderValueMatch:
      actions_.emplace_back(
          new Envoy::Router::HeaderValueMatchAction(action.header_value_match(), context));
      break;
    case ProtoRateLimit::Action::ActionSpecifierCase::kExtension: {
      ProtobufMessage::ValidationVisitor& validator = context.messageValidationVisitor();
      auto* factory =
          Envoy::Config::Utility::getFactory<Envoy::RateLimit::DescriptorProducerFactory>(
              action.extension());
      if (!factory) {
        creation_status = absl::InvalidArgumentError(
            fmt::format("rate limit action: {} is not supported", action.extension().name()));
        return;
      }
      auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
          action.extension().typed_config(), validator, *factory);
      Envoy::RateLimit::DescriptorProducerPtr producer =
          factory->createDescriptorProducerFromProto(*message, context);
      if (producer) {
        actions_.emplace_back(std::move(producer));
      } else {
        creation_status = absl::InvalidArgumentError(
            absl::StrCat("rate limit descriptor extension failed: ", action.extension().name()));
        return;
      }
      break;
    }
    case ProtoRateLimit::Action::ActionSpecifierCase::kMaskedRemoteAddress:
      actions_.emplace_back(new Router::MaskedRemoteAddressAction(action.masked_remote_address()));
      break;
    case ProtoRateLimit::Action::ActionSpecifierCase::kQueryParameterValueMatch:
      actions_.emplace_back(new Router::QueryParameterValueMatchAction(
          action.query_parameter_value_match(), context));
      break;
    case ProtoRateLimit::Action::ActionSpecifierCase::ACTION_SPECIFIER_NOT_SET:
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
  }
}

void RateLimitPolicy::populateDescriptors(const Http::RequestHeaderMap& headers,
                                          const StreamInfo::StreamInfo& stream_info,
                                          const std::string& local_service_cluster,
                                          RateLimitDescriptors& descriptors) const {
  Envoy::RateLimit::LocalDescriptor descriptor;
  for (const Envoy::RateLimit::DescriptorProducerPtr& action : actions_) {
    Envoy::RateLimit::DescriptorEntry entry;
    if (!action->populateDescriptor(entry, local_service_cluster, headers, stream_info)) {
      return;
    }
    if (!entry.key_.empty()) {
      descriptor.entries_.emplace_back(std::move(entry));
    }
  }
  descriptors.emplace_back(std::move(descriptor));
}

RateLimitConfig::RateLimitConfig(const Protobuf::RepeatedPtrField<ProtoRateLimit>& configs,
                                 Server::Configuration::CommonFactoryContext& context,
                                 absl::Status& creation_status) {
  for (const ProtoRateLimit& config : configs) {
    auto descriptor_generator = std::make_unique<RateLimitPolicy>(config, context, creation_status);
    if (!creation_status.ok()) {
      return;
    }
    rate_limit_policies_.emplace_back(std::move(descriptor_generator));
  }
}

void RateLimitConfig::populateDescriptors(const Http::RequestHeaderMap& headers,
                                          const StreamInfo::StreamInfo& stream_info,
                                          const std::string& local_service_cluster,
                                          RateLimitDescriptors& descriptors) const {
  for (const auto& generator : rate_limit_policies_) {
    generator->populateDescriptors(headers, stream_info, local_service_cluster, descriptors);
  }
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy

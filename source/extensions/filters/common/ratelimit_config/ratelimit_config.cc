#include "source/extensions/filters/common/ratelimit_config/ratelimit_config.h"

#include "source/common/config/utility.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

RateLimitPolicy::RateLimitPolicy(const ProtoRateLimit& config,
                                 Server::Configuration::CommonFactoryContext& context,
                                 absl::Status& creation_status, bool no_limit) {
  if (config.has_stage() || !config.disable_key().empty()) {
    creation_status =
        absl::InvalidArgumentError("'stage' field and 'disable_key' field are not supported");
    return;
  }

  if (config.has_limit()) {
    if (no_limit) {
      creation_status = absl::InvalidArgumentError("'limit' field is not supported");
      return;
    }
  }

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
        // If no descriptor extension is found, fallback to using HTTP matcher
        // input functions. Note that if the same extension name or type was
        // dual registered as an extension descriptor and an HTTP matcher input
        // function, the descriptor extension takes priority.
        Router::RateLimitDescriptorValidationVisitor validation_visitor;
        Matcher::MatchInputFactory<Http::HttpMatchingData> input_factory(validator,
                                                                         validation_visitor);
        Matcher::DataInputFactoryCb<Http::HttpMatchingData> data_input_cb =
            input_factory.createDataInput(action.extension());
        actions_.emplace_back(std::make_unique<Router::MatchInputRateLimitDescriptor>(
            action.extension().name(), data_input_cb()));
        break;
      }
      auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
          action.extension().typed_config(), validator, *factory);
      Envoy::RateLimit::DescriptorProducerPtr producer =
          factory->createDescriptorProducerFromProto(*message, context);
      if (producer) {
        actions_.emplace_back(std::move(producer));
      } else {
        creation_status = absl::InvalidArgumentError(
            absl::StrCat("Rate limit descriptor extension failed: ", action.extension().name()));
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
    default:
      creation_status = absl::InvalidArgumentError(fmt::format(
          "Unsupported rate limit action: {}", static_cast<int>(action.action_specifier_case())));
      return;
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
                                 absl::Status& creation_status, bool no_limit) {
  for (const ProtoRateLimit& config : configs) {
    auto descriptor_generator =
        std::make_unique<RateLimitPolicy>(config, context, creation_status, no_limit);
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

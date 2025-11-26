#include "source/extensions/filters/common/ratelimit_config/ratelimit_config.h"

#include "source/common/config/utility.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

constexpr double MAX_HITS_ADDEND = 1000000000;

RateLimitPolicy::RateLimitPolicy(const ProtoRateLimit& config,
                                 Server::Configuration::CommonFactoryContext& context,
                                 absl::Status& creation_status, bool no_limit)
    : apply_on_stream_done_(config.apply_on_stream_done()) {
  if (config.has_hits_addend()) {
    if (!config.hits_addend().format().empty()) {
      // Ensure only format or number is set.
      if (config.hits_addend().has_number()) {
        creation_status =
            absl::InvalidArgumentError("hits_addend must contain either a format or a number");
        return;
      }

      auto providers_or_error =
          Formatter::SubstitutionFormatParser::parse(config.hits_addend().format());
      SET_AND_RETURN_IF_NOT_OK(providers_or_error.status(), creation_status);
      if (providers_or_error->size() != 1) {
        creation_status =
            absl::InvalidArgumentError("hits_addend format must contain exactly one substitution");
        return;
      }
      hits_addend_provider_ = std::move(providers_or_error.value()[0]);
    } else if (config.hits_addend().has_number()) {
      hits_addend_ = config.hits_addend().number().value();
    } else {
      creation_status =
          absl::InvalidArgumentError("hits_addend must contain either a format or a number");
      return;
    }
  }

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
    case ProtoRateLimit::Action::ActionSpecifierCase::kQueryParameters:
      actions_.emplace_back(new Envoy::Router::QueryParametersAction(action.query_parameters()));
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
      absl::StatusOr<Envoy::RateLimit::DescriptorProducerPtr> producer_or =
          factory->createDescriptorProducerFromProto(*message, context);
      SET_AND_RETURN_IF_NOT_OK(producer_or.status(), creation_status);
      actions_.emplace_back(std::move(producer_or.value()));
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
  Envoy::RateLimit::Descriptor descriptor;
  for (const Envoy::RateLimit::DescriptorProducerPtr& action : actions_) {
    Envoy::RateLimit::DescriptorEntry entry;
    if (!action->populateDescriptor(entry, local_service_cluster, headers, stream_info)) {
      return;
    }
    if (!entry.key_.empty()) {
      descriptor.entries_.emplace_back(std::move(entry));
    }
  }

  // Populate hits_addend if set.
  if (hits_addend_provider_ != nullptr) {
    const Protobuf::Value hits_addend_value =
        hits_addend_provider_->formatValue({&headers}, stream_info);

    double hits_addend = 0;
    bool success = true;

    if (hits_addend_value.has_number_value()) {
      hits_addend = hits_addend_value.number_value();
    } else if (hits_addend_value.has_string_value()) {
      // Attempt to parse the string as a double.
      success = absl::SimpleAtod(hits_addend_value.string_value(), &hits_addend);
    } else {
      // Only number and string values are allowed.
      success = false;
    }

    // Check value range.
    if (hits_addend < 0 || hits_addend > MAX_HITS_ADDEND) {
      success = false;
    }

    if (success) {
      descriptor.hits_addend_ = static_cast<uint64_t>(hits_addend);
    } else {
      ENVOY_LOG_EVERY_POW_2(warn, "Invalid hits_addend: {}", hits_addend_value.DebugString());
      return;
    }

  } else if (hits_addend_.has_value()) {
    descriptor.hits_addend_ = hits_addend_.value();
  }

  descriptors.emplace_back(std::move(descriptor));
}

RateLimitConfig::RateLimitConfig(const Protobuf::RepeatedPtrField<ProtoRateLimit>& configs,
                                 Server::Configuration::CommonFactoryContext& context,
                                 absl::Status& creation_status, bool no_limit) {
  rate_limit_policies_.reserve(configs.size());
  for (const ProtoRateLimit& config : configs) {
    rate_limit_policies_.emplace_back(config, context, creation_status, no_limit);
    RETURN_ONLY_IF_NOT_OK_REF(creation_status);
  }
}

void RateLimitConfig::populateDescriptors(const Http::RequestHeaderMap& headers,
                                          const StreamInfo::StreamInfo& stream_info,
                                          const std::string& local_service_cluster,
                                          RateLimitDescriptors& descriptors,
                                          bool on_stream_done) const {
  for (const RateLimitPolicy& generator : rate_limit_policies_) {
    if (generator.applyOnStreamDone() != on_stream_done) {
      continue;
    }
    generator.populateDescriptors(headers, stream_info, local_service_cluster, descriptors);
  }
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy

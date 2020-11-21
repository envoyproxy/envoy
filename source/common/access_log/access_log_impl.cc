#include "common/access_log/access_log_impl.h"

#include <cstdint>
#include <string>

#include "envoy/common/time.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/config/accesslog/v3/accesslog.pb.validate.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"
#include "common/config/utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"
#include "common/stream_info/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace AccessLog {

ComparisonFilter::ComparisonFilter(const envoy::config::accesslog::v3::ComparisonFilter& config,
                                   Runtime::Loader& runtime)
    : config_(config), runtime_(runtime) {}

bool ComparisonFilter::compareAgainstValue(uint64_t lhs) const {
  uint64_t value = config_.value().default_value();

  if (!config_.value().runtime_key().empty()) {
    value = runtime_.snapshot().getInteger(config_.value().runtime_key(), value);
  }

  switch (config_.op()) {
  case envoy::config::accesslog::v3::ComparisonFilter::GE:
    return lhs >= value;
  case envoy::config::accesslog::v3::ComparisonFilter::EQ:
    return lhs == value;
  case envoy::config::accesslog::v3::ComparisonFilter::LE:
    return lhs <= value;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

FilterPtr FilterFactory::fromProto(const envoy::config::accesslog::v3::AccessLogFilter& config,
                                   Runtime::Loader& runtime, Random::RandomGenerator& random,
                                   ProtobufMessage::ValidationVisitor& validation_visitor) {
  switch (config.filter_specifier_case()) {
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kStatusCodeFilter:
    return FilterPtr{new StatusCodeFilter(config.status_code_filter(), runtime)};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kDurationFilter:
    return FilterPtr{new DurationFilter(config.duration_filter(), runtime)};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kNotHealthCheckFilter:
    return FilterPtr{new NotHealthCheckFilter()};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kTraceableFilter:
    return FilterPtr{new TraceableRequestFilter()};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kRuntimeFilter:
    return FilterPtr{new RuntimeFilter(config.runtime_filter(), runtime, random)};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kAndFilter:
    return FilterPtr{new AndFilter(config.and_filter(), runtime, random, validation_visitor)};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kOrFilter:
    return FilterPtr{new OrFilter(config.or_filter(), runtime, random, validation_visitor)};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kHeaderFilter:
    return FilterPtr{new HeaderFilter(config.header_filter())};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kResponseFlagFilter:
    MessageUtil::validate(config, validation_visitor);
    return FilterPtr{new ResponseFlagFilter(config.response_flag_filter())};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kGrpcStatusFilter:
    MessageUtil::validate(config, validation_visitor);
    return FilterPtr{new GrpcStatusFilter(config.grpc_status_filter())};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kMetadataFilter:
    return FilterPtr{new MetadataFilter(config.metadata_filter())};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kExtensionFilter:
    MessageUtil::validate(config, validation_visitor);
    {
      auto& factory =
          Config::Utility::getAndCheckFactory<ExtensionFilterFactory>(config.extension_filter());
      return factory.createFilter(config.extension_filter(), runtime, random);
    }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool TraceableRequestFilter::evaluate(const StreamInfo::StreamInfo& info,
                                      const Http::RequestHeaderMap& request_headers,
                                      const Http::ResponseHeaderMap&,
                                      const Http::ResponseTrailerMap&) const {
  Tracing::Decision decision = Tracing::HttpTracerUtility::isTracing(info, request_headers);

  return decision.traced && decision.reason == Tracing::Reason::ServiceForced;
}

bool StatusCodeFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap&,
                                const Http::ResponseHeaderMap&,
                                const Http::ResponseTrailerMap&) const {
  if (!info.responseCode()) {
    return compareAgainstValue(0ULL);
  }

  return compareAgainstValue(info.responseCode().value());
}

bool DurationFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap&,
                              const Http::ResponseHeaderMap&,
                              const Http::ResponseTrailerMap&) const {
  absl::optional<std::chrono::nanoseconds> final = info.requestComplete();
  ASSERT(final);

  return compareAgainstValue(
      std::chrono::duration_cast<std::chrono::milliseconds>(final.value()).count());
}

RuntimeFilter::RuntimeFilter(const envoy::config::accesslog::v3::RuntimeFilter& config,
                             Runtime::Loader& runtime, Random::RandomGenerator& random)
    : runtime_(runtime), random_(random), runtime_key_(config.runtime_key()),
      percent_(config.percent_sampled()),
      use_independent_randomness_(config.use_independent_randomness()) {}

bool RuntimeFilter::evaluate(const StreamInfo::StreamInfo& stream_info,
                             const Http::RequestHeaderMap& request_headers,
                             const Http::ResponseHeaderMap&,
                             const Http::ResponseTrailerMap&) const {
  auto rid_extension = stream_info.getRequestIDExtension();
  uint64_t random_value;
  if (use_independent_randomness_ ||
      !rid_extension->modBy(
          request_headers, random_value,
          ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent_.denominator()))) {
    random_value = random_.random();
  }

  return runtime_.snapshot().featureEnabled(
      runtime_key_, percent_.numerator(), random_value,
      ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent_.denominator()));
}

OperatorFilter::OperatorFilter(
    const Protobuf::RepeatedPtrField<envoy::config::accesslog::v3::AccessLogFilter>& configs,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  for (const auto& config : configs) {
    filters_.emplace_back(FilterFactory::fromProto(config, runtime, random, validation_visitor));
  }
}

OrFilter::OrFilter(const envoy::config::accesslog::v3::OrFilter& config, Runtime::Loader& runtime,
                   Random::RandomGenerator& random,
                   ProtobufMessage::ValidationVisitor& validation_visitor)
    : OperatorFilter(config.filters(), runtime, random, validation_visitor) {}

AndFilter::AndFilter(const envoy::config::accesslog::v3::AndFilter& config,
                     Runtime::Loader& runtime, Random::RandomGenerator& random,
                     ProtobufMessage::ValidationVisitor& validation_visitor)
    : OperatorFilter(config.filters(), runtime, random, validation_visitor) {}

bool OrFilter::evaluate(const StreamInfo::StreamInfo& info,
                        const Http::RequestHeaderMap& request_headers,
                        const Http::ResponseHeaderMap& response_headers,
                        const Http::ResponseTrailerMap& response_trailers) const {
  bool result = false;
  for (auto& filter : filters_) {
    result |= filter->evaluate(info, request_headers, response_headers, response_trailers);

    if (result) {
      break;
    }
  }

  return result;
}

bool AndFilter::evaluate(const StreamInfo::StreamInfo& info,
                         const Http::RequestHeaderMap& request_headers,
                         const Http::ResponseHeaderMap& response_headers,
                         const Http::ResponseTrailerMap& response_trailers) const {
  bool result = true;
  for (auto& filter : filters_) {
    result &= filter->evaluate(info, request_headers, response_headers, response_trailers);

    if (!result) {
      break;
    }
  }

  return result;
}

bool NotHealthCheckFilter::evaluate(const StreamInfo::StreamInfo& info,
                                    const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                    const Http::ResponseTrailerMap&) const {
  return !info.healthCheck();
}

HeaderFilter::HeaderFilter(const envoy::config::accesslog::v3::HeaderFilter& config)
    : header_data_(std::make_unique<Http::HeaderUtility::HeaderData>(config.header())) {}

bool HeaderFilter::evaluate(const StreamInfo::StreamInfo&,
                            const Http::RequestHeaderMap& request_headers,
                            const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&) const {
  return Http::HeaderUtility::matchHeaders(request_headers, *header_data_);
}

ResponseFlagFilter::ResponseFlagFilter(
    const envoy::config::accesslog::v3::ResponseFlagFilter& config) {
  for (int i = 0; i < config.flags_size(); i++) {
    absl::optional<StreamInfo::ResponseFlag> response_flag =
        StreamInfo::ResponseFlagUtils::toResponseFlag(config.flags(i));
    // The config has been validated. Therefore, every flag in the config will have a mapping.
    ASSERT(response_flag.has_value());
    configured_flags_ |= response_flag.value();
  }
}

bool ResponseFlagFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap&,
                                  const Http::ResponseHeaderMap&,
                                  const Http::ResponseTrailerMap&) const {
  if (configured_flags_ != 0) {
    return info.intersectResponseFlags(configured_flags_);
  }
  return info.hasAnyResponseFlag();
}

GrpcStatusFilter::GrpcStatusFilter(const envoy::config::accesslog::v3::GrpcStatusFilter& config) {
  for (int i = 0; i < config.statuses_size(); i++) {
    statuses_.insert(protoToGrpcStatus(config.statuses(i)));
  }

  exclude_ = config.exclude();
}

bool GrpcStatusFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap&,
                                const Http::ResponseHeaderMap& response_headers,
                                const Http::ResponseTrailerMap& response_trailers) const {

  Grpc::Status::GrpcStatus status = Grpc::Status::WellKnownGrpcStatus::Unknown;
  const auto& optional_status =
      Grpc::Common::getGrpcStatus(response_trailers, response_headers, info);
  if (optional_status.has_value()) {
    status = optional_status.value();
  }

  const bool found = statuses_.find(status) != statuses_.end();
  return exclude_ ? !found : found;
}

Grpc::Status::GrpcStatus GrpcStatusFilter::protoToGrpcStatus(
    envoy::config::accesslog::v3::GrpcStatusFilter::Status status) const {
  return static_cast<Grpc::Status::GrpcStatus>(status);
}

MetadataFilter::MetadataFilter(const envoy::config::accesslog::v3::MetadataFilter& filter_config)
    : default_match_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(filter_config, match_if_key_not_found, true)),
      filter_(filter_config.matcher().filter()) {

  if (filter_config.has_matcher()) {
    auto& matcher_config = filter_config.matcher();

    for (const auto& seg : matcher_config.path()) {
      path_.push_back(seg.key());
    }

    // Matches if the value equals the configured 'MetadataMatcher' value.
    const auto& val = matcher_config.value();
    value_matcher_ = Matchers::ValueMatcher::create(val);
  }

  // Matches if the value is present in dynamic metadata
  auto present_val = envoy::type::matcher::v3::ValueMatcher();
  present_val.set_present_match(true);
  present_matcher_ = Matchers::ValueMatcher::create(present_val);
}

bool MetadataFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap&,
                              const Http::ResponseHeaderMap&,
                              const Http::ResponseTrailerMap&) const {
  const auto& value =
      Envoy::Config::Metadata::metadataValue(&info.dynamicMetadata(), filter_, path_);
  // If the key corresponds to a set value in dynamic metadata, return true if the value matches the
  // the configured 'MetadataMatcher' value and false otherwise
  if (present_matcher_->match(value)) {
    return value_matcher_ && value_matcher_->match(value);
  }

  // If the key does not correspond to a set value in dynamic metadata, return true if
  // 'match_if_key_not_found' is set to true and false otherwise
  return default_match_;
}

InstanceSharedPtr AccessLogFactory::fromProto(const envoy::config::accesslog::v3::AccessLog& config,
                                              Server::Configuration::FactoryContext& context) {
  FilterPtr filter;
  if (config.has_filter()) {
    filter = FilterFactory::fromProto(config.filter(), context.runtime(),
                                      context.api().randomGenerator(),
                                      context.messageValidationVisitor());
  }

  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::AccessLogInstanceFactory>(config);
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      config, context.messageValidationVisitor(), factory);

  return factory.createAccessLogInstance(*message, std::move(filter), context);
}

} // namespace AccessLog
} // namespace Envoy

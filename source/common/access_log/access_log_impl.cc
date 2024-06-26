#include "source/common/access_log/access_log_impl.h"

#include <cstdint>
#include <string>

#include "envoy/common/time.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/config/accesslog/v3/accesslog.pb.validate.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/utility.h"
#include "source/common/tracing/http_tracer_impl.h"

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
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::accesslog::v3::ComparisonFilter::GE:
    return lhs >= value;
  case envoy::config::accesslog::v3::ComparisonFilter::EQ:
    return lhs == value;
  case envoy::config::accesslog::v3::ComparisonFilter::LE:
    return lhs <= value;
  }
  IS_ENVOY_BUG("unexpected comparison op enum");
  return false;
}

FilterPtr FilterFactory::fromProto(const envoy::config::accesslog::v3::AccessLogFilter& config,
                                   Server::Configuration::FactoryContext& context) {
  Runtime::Loader& runtime = context.serverFactoryContext().runtime();
  Random::RandomGenerator& random = context.serverFactoryContext().api().randomGenerator();
  ProtobufMessage::ValidationVisitor& validation_visitor = context.messageValidationVisitor();
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
    return FilterPtr{new AndFilter(config.and_filter(), context)};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kOrFilter:
    return FilterPtr{new OrFilter(config.or_filter(), context)};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kHeaderFilter:
    return FilterPtr{new HeaderFilter(config.header_filter(), context.serverFactoryContext())};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kResponseFlagFilter:
    MessageUtil::validate(config, validation_visitor);
    return FilterPtr{new ResponseFlagFilter(config.response_flag_filter())};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kGrpcStatusFilter:
    MessageUtil::validate(config, validation_visitor);
    return FilterPtr{new GrpcStatusFilter(config.grpc_status_filter())};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kMetadataFilter:
    return FilterPtr{new MetadataFilter(config.metadata_filter(), context.serverFactoryContext())};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kLogTypeFilter:
    return FilterPtr{new LogTypeFilter(config.log_type_filter())};
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::kExtensionFilter:
    MessageUtil::validate(config, validation_visitor);
    {
      auto& factory =
          Config::Utility::getAndCheckFactory<ExtensionFilterFactory>(config.extension_filter());
      return factory.createFilter(config.extension_filter(), context);
    }
  case envoy::config::accesslog::v3::AccessLogFilter::FilterSpecifierCase::FILTER_SPECIFIER_NOT_SET:
    PANIC_DUE_TO_PROTO_UNSET;
  }
  IS_ENVOY_BUG("unexpected filter specifier value");
  return nullptr;
}

bool TraceableRequestFilter::evaluate(const Formatter::HttpFormatterContext&,
                                      const StreamInfo::StreamInfo& info) const {
  const Tracing::Decision decision = Tracing::TracerUtility::shouldTraceRequest(info);
  return decision.traced && decision.reason == Tracing::Reason::ServiceForced;
}

bool StatusCodeFilter::evaluate(const Formatter::HttpFormatterContext&,
                                const StreamInfo::StreamInfo& info) const {
  if (!info.responseCode()) {
    return compareAgainstValue(0ULL);
  }

  return compareAgainstValue(info.responseCode().value());
}

bool DurationFilter::evaluate(const Formatter::HttpFormatterContext&,
                              const StreamInfo::StreamInfo& info) const {
  absl::optional<std::chrono::nanoseconds> duration = info.currentDuration();
  if (!duration.has_value()) {
    return false;
  }

  return compareAgainstValue(
      std::chrono::duration_cast<std::chrono::milliseconds>(duration.value()).count());
}

RuntimeFilter::RuntimeFilter(const envoy::config::accesslog::v3::RuntimeFilter& config,
                             Runtime::Loader& runtime, Random::RandomGenerator& random)
    : runtime_(runtime), random_(random), runtime_key_(config.runtime_key()),
      percent_(config.percent_sampled()),
      use_independent_randomness_(config.use_independent_randomness()) {}

bool RuntimeFilter::evaluate(const Formatter::HttpFormatterContext&,
                             const StreamInfo::StreamInfo& stream_info) const {
  // This code is verbose to avoid preallocating a random number that is not needed.
  uint64_t random_value;
  if (use_independent_randomness_) {
    random_value = random_.random();
  } else if (!stream_info.getStreamIdProvider().has_value()) {
    random_value = random_.random();
  } else {
    const auto rid_to_integer = stream_info.getStreamIdProvider()->toInteger();
    if (!rid_to_integer.has_value()) {
      random_value = random_.random();
    } else {
      random_value =
          rid_to_integer.value() %
          ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent_.denominator());
    }
  }

  return runtime_.snapshot().featureEnabled(
      runtime_key_, percent_.numerator(), random_value,
      ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent_.denominator()));
}

OperatorFilter::OperatorFilter(
    const Protobuf::RepeatedPtrField<envoy::config::accesslog::v3::AccessLogFilter>& configs,
    Server::Configuration::FactoryContext& context) {
  for (const auto& config : configs) {
    auto filter = FilterFactory::fromProto(config, context);
    if (filter != nullptr) {
      filters_.emplace_back(std::move(filter));
    }
  }
}

OrFilter::OrFilter(const envoy::config::accesslog::v3::OrFilter& config,
                   Server::Configuration::FactoryContext& context)
    : OperatorFilter(config.filters(), context) {}

AndFilter::AndFilter(const envoy::config::accesslog::v3::AndFilter& config,
                     Server::Configuration::FactoryContext& context)
    : OperatorFilter(config.filters(), context) {}

bool OrFilter::evaluate(const Formatter::HttpFormatterContext& context,
                        const StreamInfo::StreamInfo& info) const {
  bool result = false;
  for (auto& filter : filters_) {
    result |= filter->evaluate(context, info);

    if (result) {
      break;
    }
  }

  return result;
}

bool AndFilter::evaluate(const Formatter::HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& info) const {
  bool result = true;
  for (auto& filter : filters_) {
    result &= filter->evaluate(context, info);

    if (!result) {
      break;
    }
  }

  return result;
}

bool NotHealthCheckFilter::evaluate(const Formatter::HttpFormatterContext&,
                                    const StreamInfo::StreamInfo& info) const {
  return !info.healthCheck();
}

HeaderFilter::HeaderFilter(const envoy::config::accesslog::v3::HeaderFilter& config,
                           Server::Configuration::CommonFactoryContext& context)
    : header_data_(std::make_unique<Http::HeaderUtility::HeaderData>(config.header(), context)) {}

bool HeaderFilter::evaluate(const Formatter::HttpFormatterContext& context,
                            const StreamInfo::StreamInfo&) const {
  return Http::HeaderUtility::matchHeaders(context.requestHeaders(), *header_data_);
}

ResponseFlagFilter::ResponseFlagFilter(
    const envoy::config::accesslog::v3::ResponseFlagFilter& config)
    : has_configured_flags_(!config.flags().empty()) {

  // Preallocate the vector to avoid frequent heap allocations.
  configured_flags_.resize(StreamInfo::ResponseFlagUtils::responseFlagsVec().size(), false);
  for (int i = 0; i < config.flags_size(); i++) {
    auto response_flag = StreamInfo::ResponseFlagUtils::toResponseFlag(config.flags(i));
    // The config has been validated. Therefore, every flag in the config will have a mapping.
    ASSERT(response_flag.has_value());

    // The vector is allocated with the size of the response flags vec. Therefore, the index
    // should always be valid.
    ASSERT(response_flag.value().value() < configured_flags_.size());

    configured_flags_[response_flag.value().value()] = true;
  }
}

bool ResponseFlagFilter::evaluate(const Formatter::HttpFormatterContext&,
                                  const StreamInfo::StreamInfo& info) const {
  if (has_configured_flags_) {
    for (const auto flag : info.responseFlags()) {
      ASSERT(flag.value() < configured_flags_.size());
      if (configured_flags_[flag.value()]) {
        return true;
      }
    }
    return false;
  }
  return info.hasAnyResponseFlag();
}

GrpcStatusFilter::GrpcStatusFilter(const envoy::config::accesslog::v3::GrpcStatusFilter& config) {
  for (int i = 0; i < config.statuses_size(); i++) {
    statuses_.insert(protoToGrpcStatus(config.statuses(i)));
  }

  exclude_ = config.exclude();
}

bool GrpcStatusFilter::evaluate(const Formatter::HttpFormatterContext& context,
                                const StreamInfo::StreamInfo& info) const {

  Grpc::Status::GrpcStatus status = Grpc::Status::WellKnownGrpcStatus::Unknown;
  const auto& optional_status =
      Grpc::Common::getGrpcStatus(context.responseTrailers(), context.responseHeaders(), info);
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

LogTypeFilter::LogTypeFilter(const envoy::config::accesslog::v3::LogTypeFilter& config) {
  for (auto type_as_int : config.types()) {
    types_.insert(static_cast<AccessLogType>(type_as_int));
  }

  exclude_ = config.exclude();
}

bool LogTypeFilter::evaluate(const Formatter::HttpFormatterContext& context,
                             const StreamInfo::StreamInfo&) const {
  const bool found = types_.contains(context.accessLogType());
  return exclude_ ? !found : found;
}

MetadataFilter::MetadataFilter(const envoy::config::accesslog::v3::MetadataFilter& filter_config,
                               Server::Configuration::CommonFactoryContext& context)
    : default_match_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(filter_config, match_if_key_not_found, true)),
      filter_(filter_config.matcher().filter()) {

  if (filter_config.has_matcher()) {
    auto& matcher_config = filter_config.matcher();

    for (const auto& seg : matcher_config.path()) {
      path_.push_back(seg.key());
    }

    // Matches if the value equals the configured 'MetadataMatcher' value.
    const auto& val = matcher_config.value();
    value_matcher_ = Matchers::ValueMatcher::create(val, context);
  }

  // Matches if the value is present in dynamic metadata
  auto present_val = envoy::type::matcher::v3::ValueMatcher();
  present_val.set_present_match(true);
  present_matcher_ = Matchers::ValueMatcher::create(present_val, context);
}

bool MetadataFilter::evaluate(const Formatter::HttpFormatterContext&,
                              const StreamInfo::StreamInfo& info) const {
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
    filter = FilterFactory::fromProto(config.filter(), context);
  }

  auto& factory = Config::Utility::getAndCheckFactory<AccessLog::AccessLogInstanceFactory>(config);
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      config, context.messageValidationVisitor(), factory);

  return factory.createAccessLogInstance(*message, std::move(filter), context);
}

} // namespace AccessLog
} // namespace Envoy

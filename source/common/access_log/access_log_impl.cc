#include "common/access_log/access_log_impl.h"

#include <cstdint>
#include <string>

#include "envoy/common/time.h"
#include "envoy/config/filter/accesslog/v2/accesslog.pb.validate.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/access_log_formatter.h"
#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"
#include "common/runtime/uuid_util.h"
#include "common/stream_info/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace AccessLog {

ComparisonFilter::ComparisonFilter(
    const envoy::config::filter::accesslog::v2::ComparisonFilter& config, Runtime::Loader& runtime)
    : config_(config), runtime_(runtime) {}

bool ComparisonFilter::compareAgainstValue(uint64_t lhs) {
  uint64_t value = config_.value().default_value();

  if (!config_.value().runtime_key().empty()) {
    value = runtime_.snapshot().getInteger(config_.value().runtime_key(), value);
  }

  switch (config_.op()) {
  case envoy::config::filter::accesslog::v2::ComparisonFilter::GE:
    return lhs >= value;
  case envoy::config::filter::accesslog::v2::ComparisonFilter::EQ:
    return lhs == value;
  case envoy::config::filter::accesslog::v2::ComparisonFilter::LE:
    return lhs <= value;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

FilterPtr
FilterFactory::fromProto(const envoy::config::filter::accesslog::v2::AccessLogFilter& config,
                         Runtime::Loader& runtime, Runtime::RandomGenerator& random) {
  switch (config.filter_specifier_case()) {
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kStatusCodeFilter:
    return FilterPtr{new StatusCodeFilter(config.status_code_filter(), runtime)};
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kDurationFilter:
    return FilterPtr{new DurationFilter(config.duration_filter(), runtime)};
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kNotHealthCheckFilter:
    return FilterPtr{new NotHealthCheckFilter()};
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kTraceableFilter:
    return FilterPtr{new TraceableRequestFilter()};
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kRuntimeFilter:
    return FilterPtr{new RuntimeFilter(config.runtime_filter(), runtime, random)};
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kAndFilter:
    return FilterPtr{new AndFilter(config.and_filter(), runtime, random)};
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kOrFilter:
    return FilterPtr{new OrFilter(config.or_filter(), runtime, random)};
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kHeaderFilter:
    return FilterPtr{new HeaderFilter(config.header_filter())};
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kResponseFlagFilter:
    MessageUtil::validate(config);
    return FilterPtr{new ResponseFlagFilter(config.response_flag_filter())};
  case envoy::config::filter::accesslog::v2::AccessLogFilter::kGrpcStatusFilter:
    MessageUtil::validate(config);
    return FilterPtr{new GrpcStatusFilter(config.grpc_status_filter())};
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool TraceableRequestFilter::evaluate(const StreamInfo::StreamInfo& info,
                                      const Http::HeaderMap& request_headers,
                                      const Http::HeaderMap&, const Http::HeaderMap&) {
  Tracing::Decision decision = Tracing::HttpTracerUtility::isTracing(info, request_headers);

  return decision.traced && decision.reason == Tracing::Reason::ServiceForced;
}

bool StatusCodeFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap&,
                                const Http::HeaderMap&, const Http::HeaderMap&) {
  if (!info.responseCode()) {
    return compareAgainstValue(0ULL);
  }

  return compareAgainstValue(info.responseCode().value());
}

bool DurationFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap&,
                              const Http::HeaderMap&, const Http::HeaderMap&) {
  absl::optional<std::chrono::nanoseconds> final = info.requestComplete();
  ASSERT(final);

  return compareAgainstValue(
      std::chrono::duration_cast<std::chrono::milliseconds>(final.value()).count());
}

RuntimeFilter::RuntimeFilter(const envoy::config::filter::accesslog::v2::RuntimeFilter& config,
                             Runtime::Loader& runtime, Runtime::RandomGenerator& random)
    : runtime_(runtime), random_(random), runtime_key_(config.runtime_key()),
      percent_(config.percent_sampled()),
      use_independent_randomness_(config.use_independent_randomness()) {}

bool RuntimeFilter::evaluate(const StreamInfo::StreamInfo&, const Http::HeaderMap& request_headers,
                             const Http::HeaderMap&, const Http::HeaderMap&) {
  const Http::HeaderEntry* uuid = request_headers.RequestId();
  uint64_t random_value;
  // TODO(dnoe): Migrate uuidModBy to take string_view (#6580)
  if (use_independent_randomness_ || uuid == nullptr ||
      !UuidUtils::uuidModBy(
          std::string(uuid->value().getStringView()), random_value,
          ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent_.denominator()))) {
    random_value = random_.random();
  }

  return runtime_.snapshot().featureEnabled(
      runtime_key_, percent_.numerator(), random_value,
      ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent_.denominator()));
}

OperatorFilter::OperatorFilter(const Protobuf::RepeatedPtrField<
                                   envoy::config::filter::accesslog::v2::AccessLogFilter>& configs,
                               Runtime::Loader& runtime, Runtime::RandomGenerator& random) {
  for (const auto& config : configs) {
    filters_.emplace_back(FilterFactory::fromProto(config, runtime, random));
  }
}

OrFilter::OrFilter(const envoy::config::filter::accesslog::v2::OrFilter& config,
                   Runtime::Loader& runtime, Runtime::RandomGenerator& random)
    : OperatorFilter(config.filters(), runtime, random) {}

AndFilter::AndFilter(const envoy::config::filter::accesslog::v2::AndFilter& config,
                     Runtime::Loader& runtime, Runtime::RandomGenerator& random)
    : OperatorFilter(config.filters(), runtime, random) {}

bool OrFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap& request_headers,
                        const Http::HeaderMap& response_headers,
                        const Http::HeaderMap& response_trailers) {
  bool result = false;
  for (auto& filter : filters_) {
    result |= filter->evaluate(info, request_headers, response_headers, response_trailers);

    if (result) {
      break;
    }
  }

  return result;
}

bool AndFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap& request_headers,
                         const Http::HeaderMap& response_headers,
                         const Http::HeaderMap& response_trailers) {
  bool result = true;
  for (auto& filter : filters_) {
    result &= filter->evaluate(info, request_headers, response_headers, response_trailers);

    if (!result) {
      break;
    }
  }

  return result;
}

bool NotHealthCheckFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap&,
                                    const Http::HeaderMap&, const Http::HeaderMap&) {
  return !info.healthCheck();
}

HeaderFilter::HeaderFilter(const envoy::config::filter::accesslog::v2::HeaderFilter& config) {
  header_data_.push_back(Http::HeaderUtility::HeaderData(config.header()));
}

bool HeaderFilter::evaluate(const StreamInfo::StreamInfo&, const Http::HeaderMap& request_headers,
                            const Http::HeaderMap&, const Http::HeaderMap&) {
  return Http::HeaderUtility::matchHeaders(request_headers, header_data_);
}

ResponseFlagFilter::ResponseFlagFilter(
    const envoy::config::filter::accesslog::v2::ResponseFlagFilter& config) {
  for (int i = 0; i < config.flags_size(); i++) {
    absl::optional<StreamInfo::ResponseFlag> response_flag =
        StreamInfo::ResponseFlagUtils::toResponseFlag(config.flags(i));
    // The config has been validated. Therefore, every flag in the config will have a mapping.
    ASSERT(response_flag.has_value());
    configured_flags_ |= response_flag.value();
  }
}

bool ResponseFlagFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap&,
                                  const Http::HeaderMap&, const Http::HeaderMap&) {
  if (configured_flags_ != 0) {
    return info.intersectResponseFlags(configured_flags_);
  }
  return info.hasAnyResponseFlag();
}

GrpcStatusFilter::GrpcStatusFilter(
    const envoy::config::filter::accesslog::v2::GrpcStatusFilter& config) {
  for (int i = 0; i < config.statuses_size(); i++) {
    statuses_.insert(protoToGrpcStatus(config.statuses(i)));
  }

  exclude_ = config.exclude();
}

bool GrpcStatusFilter::evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap&,
                                const Http::HeaderMap& response_headers,
                                const Http::HeaderMap& response_trailers) {
  // The gRPC specification does not guarantee a gRPC status code will be returned from a gRPC
  // request. When it is returned, it will be in the response trailers. With that said, Envoy will
  // treat a trailers-only response as a headers-only response, so we have to check the following
  // in order:
  //   1. response_trailers gRPC status, if it exists.
  //   2. response_headers gRPC status, if it exists.
  //   3. Inferred from info HTTP status, if it exists.
  //
  // If none of those options exist, it will default to Grpc::Status::GrpcStatus::Unknown.
  const std::array<absl::optional<Grpc::Status::GrpcStatus>, 3> optional_statuses = {{
      {Grpc::Common::getGrpcStatus(response_trailers)},
      {Grpc::Common::getGrpcStatus(response_headers)},
      {info.responseCode() ? absl::optional<Grpc::Status::GrpcStatus>(
                                 Grpc::Utility::httpToGrpcStatus(info.responseCode().value()))
                           : absl::nullopt},
  }};

  Grpc::Status::GrpcStatus status = Grpc::Status::GrpcStatus::Unknown;
  for (const auto& optional_status : optional_statuses) {
    if (optional_status.has_value()) {
      status = optional_status.value();
      break;
    }
  }

  const bool found = statuses_.find(status) != statuses_.end();
  return exclude_ ? !found : found;
}

Grpc::Status::GrpcStatus GrpcStatusFilter::protoToGrpcStatus(
    envoy::config::filter::accesslog::v2::GrpcStatusFilter_Status status) const {
  return static_cast<Grpc::Status::GrpcStatus>(status);
}

InstanceSharedPtr
AccessLogFactory::fromProto(const envoy::config::filter::accesslog::v2::AccessLog& config,
                            Server::Configuration::FactoryContext& context) {
  FilterPtr filter;
  if (config.has_filter()) {
    filter = FilterFactory::fromProto(config.filter(), context.runtime(), context.random());
  }

  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::AccessLogInstanceFactory>(
          config.name());
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      config, context.messageValidationVisitor(), factory);

  return factory.createAccessLogInstance(*message, std::move(filter), context);
}

} // namespace AccessLog
} // namespace Envoy

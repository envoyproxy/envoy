#include "common/access_log/access_log_impl.h"

#include <cstdint>
#include <string>

#include "envoy/filesystem/filesystem.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/access_log_formatter.h"
#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace AccessLog {

ComparisonFilter::ComparisonFilter(
    const envoy::api::v2::filter::accesslog::ComparisonFilter& config, Runtime::Loader& runtime)
    : config_(config), runtime_(runtime) {}

bool ComparisonFilter::compareAgainstValue(uint64_t lhs) {
  uint64_t value = config_.value().default_value();

  if (!config_.value().runtime_key().empty()) {
    value = runtime_.snapshot().getInteger(config_.value().runtime_key(), value);
  }

  switch (config_.op()) {
  case envoy::api::v2::filter::accesslog::ComparisonFilter::GE:
    return lhs >= value;
  case envoy::api::v2::filter::accesslog::ComparisonFilter::EQ:
    return lhs == value;
  default:
    NOT_REACHED;
  }
}

FilterPtr FilterFactory::fromProto(const envoy::api::v2::filter::accesslog::AccessLogFilter& config,
                                   Runtime::Loader& runtime) {
  switch (config.filter_specifier_case()) {
  case envoy::api::v2::filter::accesslog::AccessLogFilter::kStatusCodeFilter:
    return FilterPtr{new StatusCodeFilter(config.status_code_filter(), runtime)};
  case envoy::api::v2::filter::accesslog::AccessLogFilter::kDurationFilter:
    return FilterPtr{new DurationFilter(config.duration_filter(), runtime)};
  case envoy::api::v2::filter::accesslog::AccessLogFilter::kNotHealthCheckFilter:
    return FilterPtr{new NotHealthCheckFilter()};
  case envoy::api::v2::filter::accesslog::AccessLogFilter::kTraceableFilter:
    return FilterPtr{new TraceableRequestFilter()};
  case envoy::api::v2::filter::accesslog::AccessLogFilter::kRuntimeFilter:
    return FilterPtr{new RuntimeFilter(config.runtime_filter(), runtime)};
  case envoy::api::v2::filter::accesslog::AccessLogFilter::kAndFilter:
    return FilterPtr{new AndFilter(config.and_filter(), runtime)};
  case envoy::api::v2::filter::accesslog::AccessLogFilter::kOrFilter:
    return FilterPtr{new OrFilter(config.or_filter(), runtime)};
  default:
    NOT_REACHED;
  }
}

bool TraceableRequestFilter::evaluate(const RequestInfo::RequestInfo& info,
                                      const Http::HeaderMap& request_headers) {
  Tracing::Decision decision = Tracing::HttpTracerUtility::isTracing(info, request_headers);

  return decision.is_tracing && decision.reason == Tracing::Reason::ServiceForced;
}

bool StatusCodeFilter::evaluate(const RequestInfo::RequestInfo& info, const Http::HeaderMap&) {
  if (!info.responseCode().valid()) {
    return compareAgainstValue(0ULL);
  }

  return compareAgainstValue(info.responseCode().value());
}

bool DurationFilter::evaluate(const RequestInfo::RequestInfo& info, const Http::HeaderMap&) {
  return compareAgainstValue(
      std::chrono::duration_cast<std::chrono::milliseconds>(info.duration()).count());
}

RuntimeFilter::RuntimeFilter(const envoy::api::v2::filter::accesslog::RuntimeFilter& config,
                             Runtime::Loader& runtime)
    : runtime_(runtime), runtime_key_(config.runtime_key()) {}

bool RuntimeFilter::evaluate(const RequestInfo::RequestInfo&,
                             const Http::HeaderMap& request_header) {
  const Http::HeaderEntry* uuid = request_header.RequestId();
  uint16_t sampled_value;
  if (uuid && UuidUtils::uuidModBy(uuid->value().c_str(), sampled_value, 100)) {
    uint64_t runtime_value =
        std::min<uint64_t>(runtime_.snapshot().getInteger(runtime_key_, 0), 100);

    return sampled_value < static_cast<uint16_t>(runtime_value);
  } else {
    return runtime_.snapshot().featureEnabled(runtime_key_, 0);
  }
}

OperatorFilter::OperatorFilter(
    const Protobuf::RepeatedPtrField<envoy::api::v2::filter::accesslog::AccessLogFilter>& configs,
    Runtime::Loader& runtime) {
  for (const auto& config : configs) {
    filters_.emplace_back(FilterFactory::fromProto(config, runtime));
  }
}

OrFilter::OrFilter(const envoy::api::v2::filter::accesslog::OrFilter& config,
                   Runtime::Loader& runtime)
    : OperatorFilter(config.filters(), runtime) {}

AndFilter::AndFilter(const envoy::api::v2::filter::accesslog::AndFilter& config,
                     Runtime::Loader& runtime)
    : OperatorFilter(config.filters(), runtime) {}

bool OrFilter::evaluate(const RequestInfo::RequestInfo& info,
                        const Http::HeaderMap& request_headers) {
  bool result = false;
  for (auto& filter : filters_) {
    result |= filter->evaluate(info, request_headers);

    if (result) {
      break;
    }
  }

  return result;
}

bool AndFilter::evaluate(const RequestInfo::RequestInfo& info,
                         const Http::HeaderMap& request_headers) {
  bool result = true;
  for (auto& filter : filters_) {
    result &= filter->evaluate(info, request_headers);

    if (!result) {
      break;
    }
  }

  return result;
}

bool NotHealthCheckFilter::evaluate(const RequestInfo::RequestInfo& info, const Http::HeaderMap&) {
  return !info.healthCheck();
}

InstanceSharedPtr
AccessLogFactory::fromProto(const envoy::api::v2::filter::accesslog::AccessLog& config,
                            Server::Configuration::FactoryContext& context) {
  FilterPtr filter;
  if (config.has_filter()) {
    filter = FilterFactory::fromProto(config.filter(), context.runtime());
  }

  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::AccessLogInstanceFactory>(
          config.name());
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(config, factory);

  return factory.createAccessLogInstance(*message, std::move(filter), context);
}

FileAccessLog::FileAccessLog(const std::string& access_log_path, FilterPtr&& filter,
                             FormatterPtr&& formatter,
                             Envoy::AccessLog::AccessLogManager& log_manager)
    : filter_(std::move(filter)), formatter_(std::move(formatter)) {
  log_file_ = log_manager.createAccessLog(access_log_path);
}

void FileAccessLog::log(const Http::HeaderMap* request_headers,
                        const Http::HeaderMap* response_headers,
                        const RequestInfo::RequestInfo& request_info) {
  static Http::HeaderMapImpl empty_headers;
  if (!request_headers) {
    request_headers = &empty_headers;
  }
  if (!response_headers) {
    response_headers = &empty_headers;
  }

  if (filter_) {
    if (!filter_->evaluate(request_info, *request_headers)) {
      return;
    }
  }

  log_file_->write(formatter_->format(*request_headers, *response_headers, request_info));
}

} // namespace AccessLog
} // namespace Envoy

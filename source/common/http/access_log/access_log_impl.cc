#include "common/http/access_log/access_log_impl.h"

#include <cstdint>
#include <string>

#include "envoy/filesystem/filesystem.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {
namespace AccessLog {

FilterImpl::FilterImpl(Json::Object& json, Runtime::Loader& runtime)
    : value_(json.getInteger("value")), runtime_(runtime) {
  std::string op = json.getString("op");
  if (op == ">=") {
    op_ = FilterOperation::GreaterEqual;
  } else {
    ASSERT(op == "=");
    op_ = FilterOperation::Equal;
  }

  if (json.hasObject("runtime_key")) {
    runtime_key_.value(json.getString("runtime_key"));
  }
}

bool FilterImpl::compareAgainstValue(uint64_t lhs) {
  uint64_t value = value_;

  if (runtime_key_.valid()) {
    value = runtime_.snapshot().getInteger(runtime_key_.value(), value);
  }

  switch (op_) {
  case FilterOperation::GreaterEqual:
    return lhs >= value;
  case FilterOperation::Equal:
    return lhs == value;
  }

  NOT_REACHED;
}

FilterPtr FilterImpl::fromJson(Json::Object& json, Runtime::Loader& runtime) {
  std::string type = json.getString("type");
  if (type == "status_code") {
    return FilterPtr{new StatusCodeFilter(json, runtime)};
  } else if (type == "duration") {
    return FilterPtr{new DurationFilter(json, runtime)};
  } else if (type == "runtime") {
    return FilterPtr{new RuntimeFilter(json, runtime)};
  } else if (type == "logical_or") {
    return FilterPtr{new OrFilter(json, runtime)};
  } else if (type == "logical_and") {
    return FilterPtr{new AndFilter(json, runtime)};
  } else if (type == "not_healthcheck") {
    return FilterPtr{new NotHealthCheckFilter()};
  } else {
    ASSERT(type == "traceable_request");
    return FilterPtr{new TraceableRequestFilter()};
  }
}

bool TraceableRequestFilter::evaluate(const RequestInfo& info, const HeaderMap& request_headers) {
  Tracing::Decision decision = Tracing::HttpTracerUtility::isTracing(info, request_headers);

  return decision.is_tracing && decision.reason == Tracing::Reason::ServiceForced;
}

bool StatusCodeFilter::evaluate(const RequestInfo& info, const HeaderMap&) {
  if (!info.responseCode().valid()) {
    return compareAgainstValue(0ULL);
  }

  return compareAgainstValue(info.responseCode().value());
}

bool DurationFilter::evaluate(const RequestInfo& info, const HeaderMap&) {
  return compareAgainstValue(info.duration().count());
}

RuntimeFilter::RuntimeFilter(Json::Object& json, Runtime::Loader& runtime)
    : runtime_(runtime), runtime_key_(json.getString("key")) {}

bool RuntimeFilter::evaluate(const RequestInfo&, const HeaderMap& request_header) {
  const HeaderEntry* uuid = request_header.RequestId();
  uint16_t sampled_value;
  if (uuid && UuidUtils::uuidModBy(uuid->value().c_str(), sampled_value, 100)) {
    uint64_t runtime_value =
        std::min<uint64_t>(runtime_.snapshot().getInteger(runtime_key_, 0), 100);

    return sampled_value < static_cast<uint16_t>(runtime_value);
  } else {
    return runtime_.snapshot().featureEnabled(runtime_key_, 0);
  }
}

OperatorFilter::OperatorFilter(const Json::Object& json, Runtime::Loader& runtime) {
  for (const Json::ObjectSharedPtr& filter : json.getObjectArray("filters")) {
    filters_.emplace_back(FilterImpl::fromJson(*filter, runtime));
  }
}

OrFilter::OrFilter(const Json::Object& json, Runtime::Loader& runtime)
    : OperatorFilter(json, runtime) {}

AndFilter::AndFilter(const Json::Object& json, Runtime::Loader& runtime)
    : OperatorFilter(json, runtime) {}

bool OrFilter::evaluate(const RequestInfo& info, const HeaderMap& request_headers) {
  bool result = false;
  for (auto& filter : filters_) {
    result |= filter->evaluate(info, request_headers);

    if (result) {
      break;
    }
  }

  return result;
}

bool AndFilter::evaluate(const RequestInfo& info, const HeaderMap& request_headers) {
  bool result = true;
  for (auto& filter : filters_) {
    result &= filter->evaluate(info, request_headers);

    if (!result) {
      break;
    }
  }

  return result;
}

bool NotHealthCheckFilter::evaluate(const RequestInfo& info, const HeaderMap&) {
  return !info.healthCheck();
}

InstanceImpl::InstanceImpl(const std::string& access_log_path, FilterPtr&& filter,
                           FormatterPtr&& formatter,
                           Envoy::AccessLog::AccessLogManager& log_manager)
    : filter_(std::move(filter)), formatter_(std::move(formatter)) {
  log_file_ = log_manager.createAccessLog(access_log_path);
}

InstanceSharedPtr InstanceImpl::fromJson(Json::Object& json, Runtime::Loader& runtime,
                                         Envoy::AccessLog::AccessLogManager& log_manager) {
  std::string access_log_path = json.getString("path");

  FilterPtr filter;
  if (json.hasObject("filter")) {
    Json::ObjectSharedPtr filterObject = json.getObject("filter");
    filter = FilterImpl::fromJson(*filterObject, runtime);
  }

  FormatterPtr formatter;
  if (json.hasObject("format")) {
    formatter.reset(new FormatterImpl(json.getString("format")));
  } else {
    formatter = AccessLogFormatUtils::defaultAccessLogFormatter();
  }

  return InstanceSharedPtr{
      new InstanceImpl(access_log_path, std::move(filter), std::move(formatter), log_manager)};
}

void InstanceImpl::log(const HeaderMap* request_headers, const HeaderMap* response_headers,
                       const RequestInfo& request_info) {
  static HeaderMapImpl empty_headers;
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

  std::string access_log_line =
      formatter_->format(*request_headers, *response_headers, request_info);
  log_file_->write(access_log_line);
}

} // AccessLog
} // Http
} // Envoy

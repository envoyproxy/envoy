#include "common/tracing/http_tracer_impl.h"

#include <string>

#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/runtime/uuid_util.h"

#include "spdlog/spdlog.h"

namespace Lyft {
namespace Tracing {

static std::string buildRequestLine(const Http::HeaderMap& request_headers,
                                    const Http::AccessLog::RequestInfo& info) {
  std::string path = request_headers.EnvoyOriginalPath()
                         ? request_headers.EnvoyOriginalPath()->value().c_str()
                         : request_headers.Path()->value().c_str();
  static const size_t max_path_length = 128;

  if (path.length() > max_path_length) {
    path = path.substr(0, max_path_length);
  }

  return fmt::format("{} {} {}", request_headers.Method()->value().c_str(), path,
                     Http::AccessLog::AccessLogFormatUtils::protocolToString(info.protocol()));
}

static std::string buildResponseCode(const Http::AccessLog::RequestInfo& info) {
  return info.responseCode().valid() ? std::to_string(info.responseCode().value()) : "0";
}

static std::string valueOrDefault(const Http::HeaderEntry* header, const char* default_value) {
  return header ? header->value().c_str() : default_value;
}

void HttpTracerUtility::mutateHeaders(Http::HeaderMap& request_headers, Runtime::Loader& runtime) {
  if (!request_headers.RequestId()) {
    return;
  }

  // TODO PERF: Avoid copy.
  std::string x_request_id = request_headers.RequestId()->value().c_str();

  uint16_t result;
  // Skip if x-request-id is corrupted.
  if (!UuidUtils::uuidModBy(x_request_id, result, 10000)) {
    return;
  }

  // Do not apply tracing transformations if we are currently tracing.
  if (UuidTraceStatus::NoTrace == UuidUtils::isTraceableUuid(x_request_id)) {
    if (request_headers.ClientTraceId() &&
        runtime.snapshot().featureEnabled("tracing.client_enabled", 100)) {
      UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Client);
    } else if (request_headers.EnvoyForceTrace()) {
      UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Forced);
    } else if (runtime.snapshot().featureEnabled("tracing.random_sampling", 10000, result, 10000)) {
      UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Sampled);
    }
  }

  if (!runtime.snapshot().featureEnabled("tracing.global_enabled", 100, result)) {
    UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::NoTrace);
  }

  request_headers.RequestId()->value(x_request_id);
}

const std::string HttpTracerUtility::INGRESS_OPERATION = "ingress";
const std::string HttpTracerUtility::EGRESS_OPERATION = "egress";

const std::string& HttpTracerUtility::toString(OperationName operation_name) {
  switch (operation_name) {
  case OperationName::Ingress:
    return INGRESS_OPERATION;
  case OperationName::Egress:
    return EGRESS_OPERATION;
  }

  NOT_REACHED
}

Decision HttpTracerUtility::isTracing(const Http::AccessLog::RequestInfo& request_info,
                                      const Http::HeaderMap& request_headers) {
  // Exclude HC requests immediately.
  if (request_info.healthCheck()) {
    return {Reason::HealthCheck, false};
  }

  if (!request_headers.RequestId()) {
    return {Reason::NotTraceableRequestId, false};
  }

  // TODO PERF: Avoid copy.
  UuidTraceStatus trace_status =
      UuidUtils::isTraceableUuid(request_headers.RequestId()->value().c_str());

  switch (trace_status) {
  case UuidTraceStatus::Client:
    return {Reason::ClientForced, true};
  case UuidTraceStatus::Forced:
    return {Reason::ServiceForced, true};
  case UuidTraceStatus::Sampled:
    return {Reason::Sampling, true};
  case UuidTraceStatus::NoTrace:
    return {Reason::NotTraceableRequestId, false};
  }

  NOT_REACHED;
}

void HttpTracerUtility::finalizeSpan(Span& active_span, const Http::HeaderMap& request_headers,
                                     const Http::AccessLog::RequestInfo& request_info,
                                     const Config& config) {
  // Pre response data.
  active_span.setTag("guid:x-request-id",
                     std::string(request_headers.RequestId()->value().c_str()));
  active_span.setTag("request_line", buildRequestLine(request_headers, request_info));
  active_span.setTag("request_size", std::to_string(request_info.bytesReceived()));
  active_span.setTag("host_header", valueOrDefault(request_headers.Host(), "-"));
  active_span.setTag("downstream_cluster",
                     valueOrDefault(request_headers.EnvoyDownstreamServiceCluster(), "-"));
  active_span.setTag("user_agent", valueOrDefault(request_headers.UserAgent(), "-"));

  if (request_headers.ClientTraceId()) {
    active_span.setTag("guid:x-client-trace-id",
                       std::string(request_headers.ClientTraceId()->value().c_str()));
  }

  // Build tags based on the custom headers.
  for (const Http::LowerCaseString& header : config.requestHeadersForTags()) {
    const Http::HeaderEntry* entry = request_headers.get(header);
    if (entry) {
      active_span.setTag(header.get(), entry->value().c_str());
    }
  }

  // Post response data.
  active_span.setTag("response_code", buildResponseCode(request_info));
  active_span.setTag("response_size", std::to_string(request_info.bytesSent()));
  active_span.setTag("response_flags",
                     Http::AccessLog::ResponseFlagUtils::toShortString(request_info));

  if (request_info.responseCode().valid() &&
      Http::CodeUtility::is5xx(request_info.responseCode().value())) {
    active_span.setTag("error", "true");
  }

  active_span.finishSpan();
}

HttpTracerImpl::HttpTracerImpl(DriverPtr&& driver, const LocalInfo::LocalInfo& local_info)
    : driver_(std::move(driver)), local_info_(local_info) {}

SpanPtr HttpTracerImpl::startSpan(const Config& config, Http::HeaderMap& request_headers,
                                  const Http::AccessLog::RequestInfo& request_info) {
  std::string span_name = HttpTracerUtility::toString(config.operationName());

  if (config.operationName() == OperationName::Egress) {
    span_name.append(" ");
    span_name.append(request_headers.Host()->value().c_str());
  }

  SpanPtr active_span = driver_->startSpan(request_headers, span_name, request_info.startTime());
  if (active_span) {
    active_span->setTag("node_id", local_info_.nodeName());
    active_span->setTag("zone", local_info_.zoneName());
  }

  return active_span;
}

} // Tracing
} // Lyft
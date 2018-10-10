#include "common/tracing/http_tracer_impl.h"

#include <string>

#include "common/access_log/access_log_formatter.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/runtime/uuid_util.h"
#include "common/stream_info/utility.h"

namespace Envoy {
namespace Tracing {

// TODO(mattklein123) PERF: Avoid string creations/copies in this entire file.
static std::string buildResponseCode(const StreamInfo::StreamInfo& info) {
  return info.responseCode() ? std::to_string(info.responseCode().value()) : "0";
}

static std::string valueOrDefault(const Http::HeaderEntry* header, const char* default_value) {
  return header ? header->value().c_str() : default_value;
}

static std::string buildUrl(const Http::HeaderMap& request_headers) {
  std::string path = request_headers.EnvoyOriginalPath()
                         ? request_headers.EnvoyOriginalPath()->value().c_str()
                         : request_headers.Path()->value().c_str();
  static const size_t max_path_length = 128;
  if (path.length() > max_path_length) {
    path = path.substr(0, max_path_length);
  }

  return fmt::format("{}://{}{}", valueOrDefault(request_headers.ForwardedProto(), ""),
                     valueOrDefault(request_headers.Host(), ""), path);
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

  NOT_REACHED_GCOVR_EXCL_LINE;
}

Decision HttpTracerUtility::isTracing(const StreamInfo::StreamInfo& stream_info,
                                      const Http::HeaderMap& request_headers) {
  // Exclude HC requests immediately.
  if (stream_info.healthCheck()) {
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

  NOT_REACHED_GCOVR_EXCL_LINE;
}

void HttpTracerUtility::finalizeSpan(Span& span, const Http::HeaderMap* request_headers,
                                     const StreamInfo::StreamInfo& stream_info,
                                     const Config& tracing_config) {
  // Pre response data.
  if (request_headers) {
    span.setTag(Tracing::Tags::get().GUID_X_REQUEST_ID,
                std::string(request_headers->RequestId()->value().c_str()));
    span.setTag(Tracing::Tags::get().HTTP_URL, buildUrl(*request_headers));
    span.setTag(Tracing::Tags::get().HTTP_METHOD, request_headers->Method()->value().c_str());
    span.setTag(Tracing::Tags::get().DOWNSTREAM_CLUSTER,
                valueOrDefault(request_headers->EnvoyDownstreamServiceCluster(), "-"));
    span.setTag(Tracing::Tags::get().USER_AGENT, valueOrDefault(request_headers->UserAgent(), "-"));
    span.setTag(Tracing::Tags::get().HTTP_PROTOCOL,
                AccessLog::AccessLogFormatUtils::protocolToString(stream_info.protocol()));

    if (request_headers->ClientTraceId()) {
      span.setTag(Tracing::Tags::get().GUID_X_CLIENT_TRACE_ID,
                  std::string(request_headers->ClientTraceId()->value().c_str()));
    }

    // Build tags based on the custom headers.
    for (const Http::LowerCaseString& header : tracing_config.requestHeadersForTags()) {
      const Http::HeaderEntry* entry = request_headers->get(header);
      if (entry) {
        span.setTag(header.get(), entry->value().c_str());
      }
    }
  }
  span.setTag(Tracing::Tags::get().REQUEST_SIZE, std::to_string(stream_info.bytesReceived()));

  if (nullptr != stream_info.upstreamHost()) {
    span.setTag(Tracing::Tags::get().UPSTREAM_CLUSTER,
                stream_info.upstreamHost()->cluster().name());
  }

  // Post response data.
  span.setTag(Tracing::Tags::get().HTTP_STATUS_CODE, buildResponseCode(stream_info));
  span.setTag(Tracing::Tags::get().RESPONSE_SIZE, std::to_string(stream_info.bytesSent()));
  span.setTag(Tracing::Tags::get().RESPONSE_FLAGS,
              StreamInfo::ResponseFlagUtils::toShortString(stream_info));

  if (!stream_info.responseCode() || Http::CodeUtility::is5xx(stream_info.responseCode().value())) {
    span.setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE);
  }

  span.finishSpan();
}

HttpTracerImpl::HttpTracerImpl(DriverPtr&& driver, const LocalInfo::LocalInfo& local_info)
    : driver_(std::move(driver)), local_info_(local_info) {}

SpanPtr HttpTracerImpl::startSpan(const Config& config, Http::HeaderMap& request_headers,
                                  const StreamInfo::StreamInfo& stream_info,
                                  const Tracing::Decision tracing_decision) {
  std::string span_name = HttpTracerUtility::toString(config.operationName());

  if (config.operationName() == OperationName::Egress) {
    span_name.append(" ");
    span_name.append(request_headers.Host()->value().c_str());
  }

  SpanPtr active_span = driver_->startSpan(config, request_headers, span_name,
                                           stream_info.startTime(), tracing_decision);
  if (active_span) {
    active_span->setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY);
    active_span->setTag(Tracing::Tags::get().NODE_ID, local_info_.nodeName());
    active_span->setTag(Tracing::Tags::get().ZONE, local_info_.zoneName());
  }

  return active_span;
}

} // namespace Tracing
} // namespace Envoy

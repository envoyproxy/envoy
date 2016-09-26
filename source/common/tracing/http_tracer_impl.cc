#include "http_tracer_impl.h"

#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/grpc/rpc_channel_impl.h"
#include "common/http/headers.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/runtime/uuid_util.h"

#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

namespace Tracing {

void HttpTracerUtility::mutateHeaders(Http::HeaderMap& request_headers, Runtime::Loader& runtime) {
  std::string x_request_id = request_headers.get(Http::Headers::get().RequestId);

  uint16_t result;
  // Skip if x-request-id is corrupted.
  if (!UuidUtils::uuidModBy(x_request_id, result, 10000)) {
    return;
  }

  if (request_headers.has(Http::Headers::get().ClientTraceId) &&
      runtime.snapshot().featureEnabled("tracing.client_enabled", 100)) {
    UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Client);
  } else if (request_headers.has(Http::Headers::get().EnvoyForceTrace)) {
    UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Forced);
  } else if (runtime.snapshot().featureEnabled("tracing.random_sampling", 0, result, 10000)) {
    UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Sampled);
  }

  if (!runtime.snapshot().featureEnabled("tracing.global_enabled", 100, result)) {
    UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::NoTrace);
  }

  request_headers.replaceViaCopy(Http::Headers::get().RequestId, x_request_id);
}

Decision HttpTracerUtility::isTracing(const Http::AccessLog::RequestInfo& request_info,
                                      const Http::HeaderMap& request_headers) {
  // Exclude HC requests immediately.
  if (request_info.healthCheck()) {
    return {Reason::HealthCheck, false};
  }

  UuidTraceStatus trace_status =
      UuidUtils::isTraceableUuid(request_headers.get(Http::Headers::get().RequestId));

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

  throw std::invalid_argument("Unknown trace_status");
}

HttpTracerImpl::HttpTracerImpl(Runtime::Loader& runtime, Stats::Store& stats)
    : runtime_(runtime),
      stats_{HTTP_TRACER_STATS(POOL_COUNTER_PREFIX(stats, "tracing.http_tracer."))} {}

void HttpTracerImpl::addSink(HttpSinkPtr&& sink) { sinks_.push_back(std::move(sink)); }

void HttpTracerImpl::trace(const Http::HeaderMap* request_headers,
                           const Http::HeaderMap* response_headers,
                           const Http::AccessLog::RequestInfo& request_info) {
  static const Http::HeaderMapImpl empty_headers;
  if (!request_headers) {
    request_headers = &empty_headers;
  }
  if (!response_headers) {
    response_headers = &empty_headers;
  }

  stats_.flush_.inc();

  Decision decision = HttpTracerUtility::isTracing(request_info, *request_headers);
  populateStats(decision);

  if (decision.is_tracing) {
    stats_.doing_tracing_.inc();

    for (HttpSinkPtr& sink : sinks_) {
      sink->flushTrace(*request_headers, *response_headers, request_info);
    }
  }
}

void HttpTracerImpl::populateStats(const Decision& decision) {
  switch (decision.reason) {
  case Reason::ClientForced:
    stats_.client_enabled_.inc();
    break;
  case Reason::HealthCheck:
    stats_.health_check_.inc();
    break;
  case Reason::NotTraceableRequestId:
    stats_.not_traceable_.inc();
    break;
  case Reason::Sampling:
    stats_.random_sampling_.inc();
    break;
  case Reason::ServiceForced:
    stats_.service_forced_.inc();
    break;
  }
}

LightStepRecorder::LightStepRecorder(LightStepSink* sink, const lightstep::TracerImpl& tracer)
    : sink_(sink), builder_(tracer) {}

void LightStepRecorder::RecordSpan(lightstep::collector::Span&& span) {
  builder_.addSpan(std::move(span));

  const int min_span_number_to_send = 5;
  if (builder_.pendingSpans() == min_span_number_to_send) {
    lightstep::collector::ReportRequest request;
    std::swap(request, builder_.pending());

    Grpc::RpcAsyncClientImpl client(sink_->clusterManager());
    client.send(sink_->collectorCluster(), "lightstep.collector.CollectorService", "Report",
                std::move(request), *this);
  }
}

bool LightStepRecorder::FlushWithTimeout(lightstep::Duration) {
  // Note: We don't expect this to be called, since the Tracer
  // reference is private to its LightStepSink.
  return true;
}

std::unique_ptr<lightstep::Recorder>
LightStepRecorder::NewInstance(LightStepSink* sink, const lightstep::TracerImpl& tracer) {
  return std::unique_ptr<lightstep::Recorder>(new LightStepRecorder(sink, tracer));
}

LightStepSink::LightStepSink(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                             const std::string& stat_prefix, Stats::Store& stats,
                             const std::string& service_node,
                             const lightstep::TracerOptions& options)
    : collector_cluster_(config.getString("collector_cluster")), cm_(cluster_manager),
      stats_{LIGHTSTEP_STATS(POOL_COUNTER_PREFIX(stats, stat_prefix + "tracing.lightstep."))},
      service_node_(service_node), options_(options) {
  if (!cm_.get(collector_cluster_)) {
    throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                     collector_cluster_));
  }
}

lightstep::Tracer& LightStepSink::thread_local_tracer() {
  static thread_local lightstep::Tracer tracer(lightstep::NewUserDefinedTransportLightStepTracer(
      options_, std::bind(&LightStepRecorder::NewInstance, this, std::placeholders::_1)));

  return tracer;
}

std::string LightStepSink::buildRequestLine(const Http::HeaderMap& request_headers,
                                            const Http::AccessLog::RequestInfo& info) {
  std::string method = request_headers.get(Http::Headers::get().Method);
  std::string path = request_headers.has(Http::Headers::get().EnvoyOriginalPath)
                         ? request_headers.get(Http::Headers::get().EnvoyOriginalPath)
                         : request_headers.get(Http::Headers::get().Path);
  static const size_t max_path_length = 256;

  if (path.length() > max_path_length) {
    path = path.substr(0, max_path_length);
  }

  return fmt::format("{} {} {}", method, path, info.protocol());
}

std::string LightStepSink::buildResponseCode(const Http::AccessLog::RequestInfo& info) {
  return info.responseCode().valid() ? std::to_string(info.responseCode().value()) : "0";
}

void LightStepSink::flushTrace(const Http::HeaderMap& request_headers,
                               const Http::HeaderMap& /*response_headers*/,
                               const Http::AccessLog::RequestInfo& request_info) {
  lightstep::Span span = thread_local_tracer().StartSpan(
      "full request",
      {
       lightstep::StartTimestamp(request_info.startTime()),
       lightstep::SetTag("join:x-request-id", request_headers.get(Http::Headers::get().RequestId)),
       lightstep::SetTag("request line", buildRequestLine(request_headers, request_info)),
       lightstep::SetTag("response code", buildResponseCode(request_info)),
       lightstep::SetTag("downstream cluster",
                         StringUtil::valueOrDefault(request_headers.get(
                             Http::Headers::get().EnvoyDownstreamServiceCluster))),
       lightstep::SetTag("user agent", StringUtil::valueOrDefault(
                                           request_headers.get(Http::Headers::get().UserAgent))),
       lightstep::SetTag("node id", service_node_),
      });

  if (request_headers.has(Http::Headers::get().ClientTraceId)) {
    span.SetTag("join:x-client-trace-id", request_headers.get(Http::Headers::get().ClientTraceId));
  }

  span.Finish();
}

} // Tracing

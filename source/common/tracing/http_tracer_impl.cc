#include "http_tracer_impl.h"

#include "common/common/macros.h"
#include "common/http/headers.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/runtime/uuid_util.h"

#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

namespace Tracing {

Decision HttpTracerUtility::isTracing(const Http::AccessLog::RequestInfo& request_info,
                                      const Http::HeaderMap& request_headers,
                                      Runtime::Loader& runtime) {
  // Exclude HC requests immediately.
  if (request_info.healthCheck()) {
    return {Reason::HealthCheck, false};
  }

  const std::string& x_request_id = request_headers.get(Http::Headers::get().RequestId);
  uint16_t result;

  // If x-request-id is corrupted then return not tracing immediately.
  if (!UuidUtils::uuidModBy(x_request_id, result, 10000)) {
    return {Reason::InvalidRequestId, false};
  }

  TraceDecision x_rq_id_trace_decision = UuidUtils::isTraceableUuid(x_request_id);

  if (x_rq_id_trace_decision.is_traced) {
    TraceReason trace_reason = x_rq_id_trace_decision.reason.value();

    if (!runtime.snapshot().featureEnabled("tracing.global_enabled", 100, result)) {
      return {Reason::GlobalSwitchOff, false};
    }

    switch (trace_reason) {
    case TraceReason::Client:
      return {Reason::ClientForced, true};
    case TraceReason::Forced:
      return {Reason::ServiceForced, true};
    case TraceReason::Sampled:
      return {Reason::Sampling, true};
    }
  }

  return {Reason::NotTraceableRequestId, false};
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

  Decision decision = HttpTracerUtility::isTracing(request_info, *request_headers, runtime_);
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
  case Reason::GlobalSwitchOff:
    stats_.global_switch_off_.inc();
    break;
  case Reason::HealthCheck:
    stats_.health_check_.inc();
    break;
  case Reason::InvalidRequestId:
    stats_.invalid_request_id_.inc();
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
  case Reason::TraceableRequest:
    stats_.traceable_.inc();
    break;
  }
}

Http::MessagePtr LightStepUtility::buildHeaders(const std::string& access_token) {
  Http::MessagePtr msg{new Http::RequestMessageImpl()};

  msg->headers().addViaCopy(Http::Headers::get().Scheme, "http");
  msg->headers().addViaCopy(Http::Headers::get().Method, "POST");
  msg->headers().addViaCopy(Http::Headers::get().Path, "/api/v0/reports");
  msg->headers().addViaCopy(Http::Headers::get().ContentType, "application/json");
  msg->headers().addViaCopy(Http::Headers::get().Host, "collector.lightstep.com");
  msg->headers().addViaCopy("LightStep-Access-Token", access_token);

  return msg;
}

std::string LightStepUtility::buildJoiningIds(const Http::HeaderMap& request_headers) {
  std::string join_ids;

  // Always populate x-request-id as joining id.
  static const std::string x_request_id_format = R"EOF(
      {{
        "TraceKey": "x-request-id",
        "Value": "{}"
      }})EOF";
  join_ids += fmt::format(x_request_id_format, request_headers.get(Http::Headers::get().RequestId));

  // Optionally populate x-client-trace-id if present.
  if (request_headers.has(Http::Headers::get().ClientTraceId)) {
    static const std::string x_client_trace_id_format = R"EOF(
      ,{{
        "TraceKey": "x-client-trace-id",
        "Value": "{}"
      }})EOF";
    join_ids += fmt::format(x_client_trace_id_format,
                            request_headers.get(Http::Headers::get().ClientTraceId));
  }

  return join_ids;
}

std::string LightStepUtility::buildRequestLine(const Http::HeaderMap& request_headers,
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

std::string LightStepUtility::buildSpanAttributes(const Http::HeaderMap& request_headers,
                                                  const Http::AccessLog::RequestInfo& request_info,
                                                  const std::string& service_node) {
  const std::string request_line = buildRequestLine(request_headers, request_info);
  std::string downstream_cluster =
      request_headers.get(Http::Headers::get().EnvoyDownstreamServiceCluster);
  if (downstream_cluster.empty()) {
    downstream_cluster = "-";
  }

  const std::string response_code = request_info.responseCode().valid()
                                        ? std::to_string(request_info.responseCode().value())
                                        : "0";
  std::string user_agent = request_headers.get(Http::Headers::get().UserAgent);
  if (user_agent.empty()) {
    user_agent = "-";
  }

  static const std::string attributes_format = R"EOF(
      {{
        "Key": "request line",
        "Value": "{}"
      }},
      {{
        "Key": "response code",
        "Value": "{}"
      }},
      {{
        "Key": "downstream cluster",
        "Value": "{}"
      }},
      {{
        "Key": "user agent",
        "Value": "{}"
      }},
      {{
        "Key": "node id",
        "Value": "{}"
      }})EOF";

  return fmt::format(attributes_format, request_line, response_code, downstream_cluster, user_agent,
                     service_node);
}

std::string LightStepUtility::buildJsonBody(const Http::HeaderMap& request_headers,
                                            const Http::HeaderMap&,
                                            const Http::AccessLog::RequestInfo& request_info,
                                            Runtime::RandomGenerator& random,
                                            const std::string& local_service_cluster,
                                            const std::string& service_node) {
  static const std::string json_format = R"EOF(
{{
  "runtime": {{
    "guid": "{}",
    "group_name": "{}",
    "start_micros": {}
  }},
  "span_records": [
    {{
      "span_guid": "{}",
      "span_name": "{}",
      "oldest_micros": {},
      "youngest_micros": {},
      "join_ids": [{}],
      "attributes": [{}]
    }}
  ]
}}
  )EOF";

  const std::string tracing_guid = random.uuid();
  static const std::string group_name = "Envoy-Tracing";
  uint64_t start_time = std::chrono::duration_cast<std::chrono::microseconds>(
                            request_info.startTime().time_since_epoch()).count();
  const std::string start_micros = std::to_string(start_time);
  const std::string span_guid = random.uuid();
  const std::string& span_name = local_service_cluster;
  const std::string oldest_micros = start_micros;
  uint64_t end_time =
      start_time +
      std::chrono::duration_cast<std::chrono::microseconds>(request_info.duration()).count();
  const std::string youngest_micros = std::to_string(end_time);
  const std::string joining_ids = buildJoiningIds(request_headers);
  const std::string annotations = buildSpanAttributes(request_headers, request_info, service_node);

  return fmt::format(json_format, tracing_guid, group_name, start_micros, span_guid, span_name,
                     oldest_micros, youngest_micros, joining_ids, annotations);
}

LightStepSink::LightStepSink(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                             const std::string& stat_prefix, Stats::Store& stats,
                             Runtime::RandomGenerator& random,
                             const std::string& local_service_cluster,
                             const std::string& service_node, const std::string& access_token)
    : collector_cluster_(config.getString("collector_cluster")), cm_(cluster_manager),
      stats_{LIGHTSTEP_STATS(POOL_COUNTER_PREFIX(stats, stat_prefix + "tracing.lightstep."))},
      random_(random), local_service_cluster_(local_service_cluster), service_node_(service_node),
      access_token_(access_token) {
  if (!cm_.get(collector_cluster_)) {
    throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                     collector_cluster_));
  }
}

void LightStepSink::flushTrace(const Http::HeaderMap& request_headers,
                               const Http::HeaderMap& response_headers,
                               const Http::AccessLog::RequestInfo& request_info) {
  Http::MessagePtr msg = LightStepUtility::buildHeaders(access_token_);
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl(
      LightStepUtility::buildJsonBody(request_headers, response_headers, request_info, random_,
                                      local_service_cluster_, service_node_)));

  msg->body(std::move(buffer));
  executeRequest(std::move(msg));
}

void LightStepSink::executeRequest(Http::MessagePtr&& msg) {
  cm_.httpAsyncClientForCluster(collector_cluster_)
      .send(std::move(msg), *this, std::chrono::milliseconds(5000));
}

void LightStepSink::onFailure(Http::AsyncClient::FailureReason) { stats_.collector_failed_.inc(); }

void LightStepSink::onSuccess(Http::MessagePtr&&) { stats_.collector_success_.inc(); }

} // Tracing
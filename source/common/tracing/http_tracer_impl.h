#pragma once

#include "envoy/runtime/runtime.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

namespace Tracing {

#define LIGHTSTEP_STATS(COUNTER)                                                                   \
  COUNTER(collector_failed)                                                                        \
  COUNTER(collector_success)

struct LightStepStats {
  LIGHTSTEP_STATS(GENERATE_COUNTER_STRUCT)
};

#define HTTP_TRACER_STATS(COUNTER)                                                                 \
  COUNTER(global_switch_off)                                                                       \
  COUNTER(invalid_request_id)                                                                      \
  COUNTER(random_sampling)                                                                         \
  COUNTER(service_forced)                                                                          \
  COUNTER(client_enabled)                                                                          \
  COUNTER(doing_tracing)                                                                           \
  COUNTER(flush)                                                                                   \
  COUNTER(not_traceable)                                                                           \
  COUNTER(health_check)                                                                            \
  COUNTER(traceable)

struct HttpTracerStats {
  HTTP_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

class HttpNullTracer : public HttpTracer {
public:
  // Tracing::HttpTracer
  void addSink(HttpSinkPtr&&) override {}
  void trace(const Http::HeaderMap*, const Http::HeaderMap*,
             const Http::AccessLog::RequestInfo&) override {}
};

enum class Reason {
  InvalidRequestId,
  NotTraceableRequestId,
  HealthCheck,
  GlobalSwitchOff,
  Sampling,
  ServiceForced,
  ClientForced,
  TraceableRequest
};

struct Decision {
  Reason reason;
  bool is_tracing;
};

class HttpTracerUtility {
public:
  /**
   * Request might be traceable if x-request-id is traceable uuid or we do sampling tracing.
   * Note: there is a global switch which turns off tracing completely on server side.
   *
   * @return decision if request is traceable or not and Reason why.
   **/
  static Decision isTracing(const Http::AccessLog::RequestInfo& request_info,
                            const Http::HeaderMap& request_headers, Runtime::Loader& runtime);
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(Runtime::Loader& runtime, Stats::Store& stats);

  // Tracing::HttpTracer
  void addSink(HttpSinkPtr&& sink) override;
  void trace(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
             const Http::AccessLog::RequestInfo& request_info) override;

private:
  void populateStats(const Decision& decision);

  Runtime::Loader& runtime_;
  HttpTracerStats stats_;
  std::vector<HttpSinkPtr> sinks_;
};

class LightStepUtility {
public:
  /**
   * Sample json body format.
   *
   * {"runtime": { "guid": "039e9da7-2a07-4a54-9440-eaee8f3887ea", "group_name": "Envoy-Test",
   * "start_micros": 1466704630010000 }, "span_records": [ { "span_guid":
   * "745bfab3-4ba4-4a36-9133-ecf76108feb5", "span_name": "front-envoy",
   * "oldest_micros":1466704630010000, "youngest_micros": 1466704630500000, "join_ids": [ {
   * "TraceKey": "x-request-id", "Value": "5463a0cc-e469-454c-a8ac-950dd1a87c66" }, {
   * "TraceKey": "x-client-request-id", "Value": "fcd405fd-657e-40e7-adf3-27c306df60a3" }]}]}
   **/
  static std::string
  buildJsonBody(const Http::HeaderMap& request_headers, const Http::HeaderMap& response_headers,
                const Http::AccessLog::RequestInfo& request_info, Runtime::RandomGenerator& random,
                const std::string& local_service_cluster, const std::string& service_node);

  /**
   * Create LightStep specific headers.
   *
   * Note: We temporary keep access token to LightStep here hardcoded.
   * This needs to be retrieved from Confidant, but we only can do so when we move LightStep
   * collectors to our internal service.
   *
   * @param access token for light step access.
   */
  static Http::MessagePtr buildHeaders(const std::string& access_token);

private:
  /**
   * Build request line: Method Request-URI Protocol.
   * Note: Request-URI will be truncated if it's longer than 256 chars.
   */
  static std::string buildRequestLine(const Http::HeaderMap& request_headers,
                                      const Http::AccessLog::RequestInfo& request_info);
  static std::string buildJoiningIds(const Http::HeaderMap& request_headers);
  static std::string buildSpanAttributes(const Http::HeaderMap& request_headers,
                                         const Http::AccessLog::RequestInfo& request_info,
                                         const std::string& service_node);
};

/**
 * LightStep (http://lightstep.com/) provides tracing capabilities, aggregation, visualization of
 * application trace data.
 *
 * LightStepSink is for flushing data to LightStep collectors.
 */
class LightStepSink : public HttpSink, public Http::AsyncClient::Callbacks {
public:
  LightStepSink(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                const std::string& stat_prefix, Stats::Store& stats,
                Runtime::RandomGenerator& random, const std::string& local_service_cluster,
                const std::string& service_node, const std::string& access_token);

  // Tracer::HttpSink
  void flushTrace(const Http::HeaderMap& request_headers, const Http::HeaderMap& response_headers,
                  const Http::AccessLog::RequestInfo& request_info) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&&) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

private:
  void executeRequest(Http::MessagePtr&& msg);

  const std::string collector_cluster_;
  Upstream::ClusterManager& cm_;
  LightStepStats stats_;
  Runtime::RandomGenerator& random_;
  const std::string local_service_cluster_;
  const std::string service_node_;
  const std::string access_token_;
};

} // Tracing
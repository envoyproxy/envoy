#pragma once

#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

#include "lightstep/envoy.h"
#include "lightstep/tracer.h"

namespace Tracing {

#define LIGHTSTEP_TRACER_STATS(COUNTER)                                                            \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)

struct LightstepTracerStats {
  LIGHTSTEP_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

enum class Reason {
  NotTraceableRequestId,
  HealthCheck,
  Sampling,
  ServiceForced,
  ClientForced,
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
                            const Http::HeaderMap& request_headers);

  /**
   * Mutate request headers if request needs to be traced.
   */
  static void mutateHeaders(Http::HeaderMap& request_headers, Runtime::Loader& runtime);

  /**
   * 1) Fill in span tags based on the response headers.
   * 2) Finish active span.
   */
  static void finalizeSpan(Span& active_span, const Http::HeaderMap& request_headers,
                           const Http::AccessLog::RequestInfo& request_info);
};

class HttpNullTracer : public HttpTracer {
public:
  // Tracing::HttpTracer
  SpanPtr startSpan(const Config&, Http::HeaderMap&, const Http::AccessLog::RequestInfo&) override {
    return nullptr;
  }
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(DriverPtr&& driver, const LocalInfo::LocalInfo& local_info);

  // Tracing::HttpTracer
  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const Http::AccessLog::RequestInfo& request_info) override;

private:
  DriverPtr driver_;
  const LocalInfo::LocalInfo& local_info_;
};

class LightStepSpan : public Span {
public:
  LightStepSpan(lightstep::Span& span);

  // Tracing::Span
  void finishSpan() override;
  void setTag(const std::string& name, const std::string& value) override;

  lightstep::SpanContext context() { return span_.context(); }

private:
  lightstep::Span span_;
};

typedef std::unique_ptr<LightStepSpan> LightStepSpanPtr;

/**
 * LightStep (http://lightstep.com/) provides tracing capabilities, aggregation, visualization of
 * application trace data.
 *
 * LightStepSink is for flushing data to LightStep collectors.
 */
class LightStepDriver : public Driver {
public:
  LightStepDriver(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                  Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                  std::unique_ptr<lightstep::TracerOptions> options);

  // Tracer::TracingDriver
  SpanPtr startSpan(Http::HeaderMap& request_headers, const std::string& operation_name,
                    SystemTime start_time) override;

  Upstream::ClusterManager& clusterManager() { return cm_; }
  Upstream::ClusterInfoPtr cluster() { return cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  LightstepTracerStats& tracerStats() { return tracer_stats_; }

private:
  struct TlsLightStepTracer : ThreadLocal::ThreadLocalObject {
    TlsLightStepTracer(lightstep::Tracer tracer, LightStepDriver& driver);

    void shutdown() override {}

    lightstep::Tracer tracer_;
    LightStepDriver& driver_;
  };

  Upstream::ClusterManager& cm_;
  Upstream::ClusterInfoPtr cluster_;
  LightstepTracerStats tracer_stats_;
  ThreadLocal::Instance& tls_;
  Runtime::Loader& runtime_;
  std::unique_ptr<lightstep::TracerOptions> options_;
  uint32_t tls_slot_;
};

class LightStepRecorder : public lightstep::Recorder, Http::AsyncClient::Callbacks {
public:
  LightStepRecorder(const lightstep::TracerImpl& tracer, LightStepDriver& driver,
                    Event::Dispatcher& dispatcher);

  // lightstep::Recorder
  void RecordSpan(lightstep::collector::Span&& span) override;
  bool FlushWithTimeout(lightstep::Duration) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&&) override;
  void onFailure(Http::AsyncClient::FailureReason) override;

  static std::unique_ptr<lightstep::Recorder> NewInstance(LightStepDriver& driver,
                                                          Event::Dispatcher& dispatcher,
                                                          const lightstep::TracerImpl& tracer);

private:
  void enableTimer();
  void flushSpans();

  lightstep::ReportBuilder builder_;
  LightStepDriver& driver_;
  Event::TimerPtr flush_timer_;
};

} // Tracing

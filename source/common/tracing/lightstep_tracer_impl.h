#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/tracing/opentracing_driver_impl.h"

#include "lightstep/tracer.h"
#include "lightstep/transporter.h"
#include "opentracing/noop.h"
#include "opentracing/tracer.h"

namespace Envoy {
namespace Tracing {

#define LIGHTSTEP_TRACER_STATS(COUNTER)                                                            \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)

struct LightstepTracerStats {
  LIGHTSTEP_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * LightStepLogger is used to translate logs generated from LightStep's tracer to Envoy logs.
 */
class LightStepLogger : Logger::Loggable<Logger::Id::tracing> {
public:
  void operator()(lightstep::LogLevel level, opentracing::string_view message) const;
};

/**
 * LightStep (http://lightstep.com/) provides tracing capabilities, aggregation, visualization of
 * application trace data.
 *
 * LightStepSink is for flushing data to LightStep collectors.
 */
class LightStepDriver : public OpenTracingDriver {
public:
  LightStepDriver(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                  Stats::Store& stats, ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                  std::unique_ptr<lightstep::LightStepTracerOptions>&& options,
                  PropagationMode propagation_mode);

  Upstream::ClusterManager& clusterManager() { return cm_; }
  Upstream::ClusterInfoConstSharedPtr cluster() { return cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  LightstepTracerStats& tracerStats() { return tracer_stats_; }

  // Tracer::OpenTracingDriver
  opentracing::Tracer& tracer() override;
  PropagationMode propagationMode() const override { return propagation_mode_; }

private:
  class LightStepTransporter : public lightstep::AsyncTransporter, Http::AsyncClient::Callbacks {
  public:
    explicit LightStepTransporter(LightStepDriver& driver);

    ~LightStepTransporter();

    // lightstep::AsyncTransporter
    void Send(const Protobuf::Message& request, Protobuf::Message& response,
              lightstep::AsyncTransporter::Callback& callback) override;

    // Http::AsyncClient::Callbacks
    void onSuccess(Http::MessagePtr&& response) override;
    void onFailure(Http::AsyncClient::FailureReason) override;

  private:
    Http::AsyncClient::Request* active_request_ = nullptr;
    lightstep::AsyncTransporter::Callback* active_callback_ = nullptr;
    Protobuf::Message* active_response_ = nullptr;
    LightStepDriver& driver_;
  };

  class LightStepMetricsObserver : public ::lightstep::MetricsObserver {
  public:
    explicit LightStepMetricsObserver(LightStepDriver& driver);

    void OnSpansSent(int num_spans) override;

  private:
    LightStepDriver& driver_;
  };

  class TlsLightStepTracer : public ThreadLocal::ThreadLocalObject {
  public:
    TlsLightStepTracer(const std::shared_ptr<lightstep::LightStepTracer>& tracer,
                       LightStepDriver& driver, Event::Dispatcher& dispatcher);

    opentracing::Tracer& tracer();

  private:
    void enableTimer();

    std::shared_ptr<lightstep::LightStepTracer> tracer_;
    LightStepDriver& driver_;
    Event::TimerPtr flush_timer_;
  };

  Upstream::ClusterManager& cm_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  LightstepTracerStats tracer_stats_;
  ThreadLocal::SlotPtr tls_;
  Runtime::Loader& runtime_;
  std::unique_ptr<lightstep::LightStepTracerOptions> options_;
  const PropagationMode propagation_mode_;
};

} // Tracing
} // namespace Envoy

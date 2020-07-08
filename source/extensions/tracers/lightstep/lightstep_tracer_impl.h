#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/trace/v3/lightstep.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/grpc/context_impl.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/stats/symbol_table_impl.h"
#include "common/upstream/cluster_update_tracker.h"

#include "extensions/tracers/common/ot/opentracing_driver_impl.h"

#include "lightstep/tracer.h"
#include "lightstep/transporter.h"
#include "opentracing/noop.h"
#include "opentracing/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

#define LIGHTSTEP_TRACER_STATS(COUNTER)                                                            \
  COUNTER(spans_sent)                                                                              \
  COUNTER(spans_dropped)                                                                           \
  COUNTER(timer_flushed)                                                                           \
  COUNTER(reports_skipped_no_cluster)

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
class LightStepDriver : public Common::Ot::OpenTracingDriver {
public:
  LightStepDriver(const envoy::config::trace::v3::LightstepConfig& lightstep_config,
                  Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                  ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                  std::unique_ptr<lightstep::LightStepTracerOptions>&& options,
                  PropagationMode propagation_mode, Grpc::Context& grpc_context);

  Upstream::ClusterManager& clusterManager() { return cm_; }
  const std::string& cluster() { return cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  LightstepTracerStats& tracerStats() { return tracer_stats_; }

  static const size_t DefaultMinFlushSpans;

  void flush();

  // Tracer::OpenTracingDriver
  opentracing::Tracer& tracer() override;
  PropagationMode propagationMode() const override { return propagation_mode_; }

private:
  class LightStepTransporter : Logger::Loggable<Logger::Id::tracing>,
                               public lightstep::AsyncTransporter,
                               public Http::AsyncClient::Callbacks {
  public:
    explicit LightStepTransporter(LightStepDriver& driver);

    ~LightStepTransporter() override;

    // lightstep::AsyncTransporter
    void OnSpanBufferFull() noexcept override;

    void Send(std::unique_ptr<lightstep::BufferChain>&& message,
              Callback& callback) noexcept override;

    // Http::AsyncClient::Callbacks
    void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request&,
                   Http::AsyncClient::FailureReason failure_reason) override;
    void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

  private:
    std::unique_ptr<lightstep::BufferChain> active_report_;
    Callback* active_callback_ = nullptr;
    Upstream::ClusterInfoConstSharedPtr active_cluster_;
    Http::AsyncClient::Request* active_request_ = nullptr;
    LightStepDriver& driver_;
    Upstream::ClusterUpdateTracker collector_cluster_;

    void reset();
  };

  class LightStepMetricsObserver : public ::lightstep::MetricsObserver {
  public:
    explicit LightStepMetricsObserver(LightStepDriver& driver);

    void OnSpansSent(int num_spans) noexcept override;

    void OnSpansDropped(int num_spans) noexcept override;

  private:
    LightStepDriver& driver_;
  };

  class TlsLightStepTracer : public ThreadLocal::ThreadLocalObject {
  public:
    TlsLightStepTracer(const std::shared_ptr<lightstep::LightStepTracer>& tracer,
                       LightStepDriver& driver, Event::Dispatcher& dispatcher);

    lightstep::LightStepTracer& tracer();

    void enableTimer();

  private:
    std::shared_ptr<lightstep::LightStepTracer> tracer_;
    LightStepDriver& driver_;
    Event::TimerPtr flush_timer_;
  };

  Upstream::ClusterManager& cm_;
  std::string cluster_;
  LightstepTracerStats tracer_stats_;
  ThreadLocal::SlotPtr tls_;
  Runtime::Loader& runtime_;
  std::unique_ptr<lightstep::LightStepTracerOptions> options_;
  const PropagationMode propagation_mode_;
  Grpc::Context& grpc_context_;
  Stats::StatNamePool pool_;
  const Grpc::Context::RequestStatNames request_stat_names_;
};
} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

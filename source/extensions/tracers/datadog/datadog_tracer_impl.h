#pragma once

#include <datadog/opentracing.h>

#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

#include "extensions/tracers/common/ot/opentracing_driver_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

#define DATADOG_TRACER_STATS(COUNTER)                                                              \
  COUNTER(traces_sent)                                                                             \
  COUNTER(timer_flushed)                                                                           \
  COUNTER(reports_sent)                                                                            \
  COUNTER(reports_dropped)                                                                         \
  COUNTER(reports_failed)

struct DatadogTracerStats {
  DATADOG_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

class TraceReporter;
using TraceReporterPtr = std::unique_ptr<TraceReporter>;
using TraceEncoderSharedPtr = std::shared_ptr<datadog::opentracing::TraceEncoder>;

/**
 * Class for a Datadog-specific Driver.
 */
class Driver : public Common::Ot::OpenTracingDriver {
public:
  /**
   * Constructor. It adds itself and a newly-created Datadog::Tracer object to a thread-local store.
   */
  Driver(const envoy::config::trace::v3::DatadogConfig& datadog_config,
         Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
         ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime);

  // Getters to return the DatadogDriver's key members.
  Upstream::ClusterManager& clusterManager() { return cm_; }
  Upstream::ClusterInfoConstSharedPtr cluster() { return cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  DatadogTracerStats& tracerStats() { return tracer_stats_; }
  const datadog::opentracing::TracerOptions& tracerOptions() { return tracer_options_; }

  // Tracer::OpenTracingDriver
  opentracing::Tracer& tracer() override;
  PropagationMode propagationMode() const override {
    return Common::Ot::OpenTracingDriver::PropagationMode::TracerNative;
  }

private:
  /**
   * Thread-local store containing DatadogDriver and Datadog::Tracer objects.
   */
  struct TlsTracer : ThreadLocal::ThreadLocalObject {
    TlsTracer(const std::shared_ptr<opentracing::Tracer>& tracer, TraceReporterPtr&& reporter,
              Driver& driver);

    std::shared_ptr<opentracing::Tracer> tracer_;
    TraceReporterPtr reporter_;
    Driver& driver_;
  };

  Upstream::ClusterManager& cm_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  DatadogTracerStats tracer_stats_;
  datadog::opentracing::TracerOptions tracer_options_;
  ThreadLocal::SlotPtr tls_;
  Runtime::Loader& runtime_;
};

/**
 * This class wraps the encoder provided with the tracer at initialization
 * and uses Http::AsyncClient to send completed traces to the Datadog Agent.
 *
 * The cluster to use for submitting traces to the agent is controlled with
 * the setting tracing.datadog.collector_cluster, which is mandatory and must
 * refer to a cluster in the active configuration.
 *
 * An internal timer is used to control how often traces are submitted.
 * If zero traces have completed in the interval between timer events,
 * no action is taken.
 * The timer interval can be controlled with the setting
 * tracing.datadog.flush_interval_ms, and defaults to 2000ms.
 */
class TraceReporter : public Http::AsyncClient::Callbacks,
                      protected Logger::Loggable<Logger::Id::tracing> {
public:
  /**
   * Constructor.
   *
   * @param encoder Provides methods to retrieve data for publishing traces.
   * @param driver The driver to be associated with the reporter.
   * @param dispatcher Controls the timer used to flush buffered traces.
   */
  TraceReporter(TraceEncoderSharedPtr encoder, Driver& driver, Event::Dispatcher& dispatcher);

  // Http::AsyncClient::Callbacks.
  void onSuccess(Http::ResponseMessagePtr&&) override;
  void onFailure(Http::AsyncClient::FailureReason) override;

private:
  /**
   * Enables the trace-flushing timer.
   */
  void enableTimer();

  /**
   * Removes all traces from the trace buffer and sends them to a Datadog Agent using
   * Http::AsyncClient.
   */
  void flushTraces();

  Driver& driver_;
  Event::TimerPtr flush_timer_;
  TraceEncoderSharedPtr encoder_;

  std::map<std::string, Http::LowerCaseString> lower_case_headers_;
};
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

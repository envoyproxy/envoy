#pragma once

#include <optional>
#include <string>

#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/datadog/tracer_stats.h"

#include "datadog/tracer.h"

namespace datadog {
namespace tracing {

class DictReader;
class FinalizedTracerConfig;
class Span;
struct SpanConfig;
struct TracerConfig;

} // namespace tracing
} // namespace datadog

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

/**
 * Entry point for Datadog tracing. This class accepts configuration and runtime
 * dependencies, and installs a thread-local instance of itself from which
 * traces can be created.
 */
class Tracer : public Tracing::Driver, private Logger::Loggable<Logger::Id::tracing> {
public:
  /**
   * Create a \c Tracer that produces traces according to the specified \p config
   * and sends those traces to the specified \p collector_cluster, uses the
   * specified \p collector_reference_host as the "Host" header, uses the
   * specified \p cluster_manager to send requests to the collector, uses the
   * specified \p scope to keep track of usage statistics, and uses the specified
   * \p thread_local_slot_allocator to register this instance with the current
   * worker thread.
   * @param collector_cluster name of the cluster to which traces will be sent
   * @param collector_reference_host value of the "Host" header in requests sent
   * to the \p collector_cluster.
   * @param config Datadog tracer configuration
   * @param cluster_manager cluster manager from which the thread local
   * cluster is retrieved in order to send HTTP request containing traces
   * @param scope statistics scope from which \c TracerStats can be created
   * @param thread_local_slot_allocator slot allocator for installing a
   * thread-local instance of the tracer.
   */
  explicit Tracer(const std::string& collector_cluster, const std::string& collector_reference_host,
                  const datadog::tracing::TracerConfig& config,
                  Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                  ThreadLocal::SlotAllocator& thread_local_slot_allocator);

  struct ThreadLocalTracer : public ThreadLocal::ThreadLocalObject {
    /**
     * Create a thread local tracer configured using the specified `config`.
     */
    explicit ThreadLocalTracer(const datadog::tracing::FinalizedTracerConfig& config);

    /**
     * Create a null (no-op) thread local tracer.
     */
    ThreadLocalTracer() = default;

    datadog::tracing::Optional<datadog::tracing::Tracer> tracer;
  };

  // Tracing::Driver

  /**
   * Create a Datadog span from the specified \p trace_context, and having the
   * specified \p operation_name, \p stream_info and \p tracing_decision.
   * If this tracer encountered an error during initialization, then return a
   * \c Tracing::NullSpan instead.
   * @param config this parameter is ignored
   * @param trace_context possibly contains information about an existing trace
   * that the returned span will be a part of; otherwise, the returned span is
   * the root of a new trace
   * @param stream_info contains information about the stream.
   * @param operation_name the Datadog "resource name" to associate with the span.
   * See comments in the implementation for more information.
   * @param tracing_decision the sampling decision made in advance by Envoy for
   * this trace. If the decision is to drop the trace, then this tracer will
   * honor that decision. If the decision is to keep the trace, then this tracer
   * will apply its own sampling logic, which might keep or drop the trace.
   */
  Tracing::SpanPtr startSpan(const Tracing::Config& config, Tracing::TraceContext& trace_context,
                             const StreamInfo::StreamInfo& stream_info,
                             const std::string& operation_name,
                             Tracing::Decision tracing_decision) override;

private:
  datadog::tracing::Span extract_or_create_span(datadog::tracing::Tracer& tracer,
                                                const datadog::tracing::SpanConfig& span_config,
                                                const datadog::tracing::DictReader& reader);

  TracerStats tracer_stats_;
  ThreadLocal::TypedSlotPtr<ThreadLocalTracer> thread_local_slot_;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

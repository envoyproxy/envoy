#pragma once

#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <optional>
#include <string>

#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/datadog/dd.h"
#include "source/extensions/tracers/datadog/tracer_stats.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

class Tracer : public Tracing::Driver, private Logger::Loggable<Logger::Id::tracing> {
public:
  // Create a `Tracer` that produces traces according to the specified `config`
  // and sends those traces to the specified `collector_cluster`, uses the
  // specified `collector_reference_host` as the "Host" header, uses the
  // specified `cluster_manager` to send requests to the collector, uses the
  // specified `scope` to keep track of usage statistics, and uses the specified
  // `thread_local_slot_allocator` to register this instance with the current
  // worker thread.
  explicit Tracer(const std::string& collector_cluster, const std::string& collector_reference_host,
                  const dd::TracerConfig& config, Upstream::ClusterManager& cluster_manager,
                  Stats::Scope& scope, ThreadLocal::SlotAllocator& thread_local_slot_allocator);

  struct ThreadLocalTracer : public ThreadLocal::ThreadLocalObject {
    // Create a thread local tracer configured using the specified `config`.
    explicit ThreadLocalTracer(const dd::FinalizedTracerConfig& config);

    // Create a null (no-op) thread local tracer.
    ThreadLocalTracer() = default;

    dd::Optional<dd::Tracer> tracer;
  };

  // Tracer::TracingDriver

  Tracing::SpanPtr startSpan(const Tracing::Config& config, Tracing::TraceContext& trace_context,
                             const std::string& operation_name, SystemTime start_time,
                             const Tracing::Decision tracing_decision) override;

private:
  TracerStats tracer_stats_;
  ThreadLocal::SlotPtr thread_local_slot_;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

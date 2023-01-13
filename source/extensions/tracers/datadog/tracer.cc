#include "source/extensions/tracers/datadog/tracer.h"

#include <datadog/dict_reader.h>
#include <datadog/error.h>
#include <datadog/sampling_priority.h>
#include <datadog/span_config.h>
#include <datadog/trace_segment.h>

#include <cassert>
#include <memory>
#include <utility>

#include "envoy/tracing/trace_context.h"

#include "source/common/config/utility.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/agent_http_client.h"
#include "source/extensions/tracers/datadog/dict_util.h"
#include "source/extensions/tracers/datadog/event_scheduler.h"
#include "source/extensions/tracers/datadog/logger.h"
#include "source/extensions/tracers/datadog/span.h"
#include "source/extensions/tracers/datadog/time_util.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

ThreadLocal::ThreadLocalObjectSharedPtr
makeThreadLocalTracer(dd::TracerConfig config, Upstream::ClusterManager& cluster_manager,
                      const std::string& collector_cluster,
                      const std::string& collector_reference_host, TracerStats& tracer_stats,
                      Event::Dispatcher& dispatcher, spdlog::logger& logger) {
  config.logger = std::make_shared<Logger>(logger);
  config.agent.event_scheduler = std::make_shared<EventScheduler>(dispatcher);
  config.agent.http_client = std::make_shared<AgentHTTPClient>(
      cluster_manager, collector_cluster, collector_reference_host, tracer_stats);

  auto maybe_config = dd::finalize_config(config);
  if (auto* error = maybe_config.if_error()) {
    auto prefix = "Unable to configure Datadog tracer. Tracing is now disabled. Error: ";
    config.logger->log_error(error->with_prefix(prefix));
    return std::make_shared<Tracer::ThreadLocalTracer>();
  }

  return std::make_shared<Tracer::ThreadLocalTracer>(*maybe_config);
}

} // namespace

Tracer::ThreadLocalTracer::ThreadLocalTracer(const dd::FinalizedTracerConfig& config)
    : tracer(config) {}

Tracer::Tracer(const std::string& collector_cluster, const std::string& collector_reference_host,
               const dd::TracerConfig& config, Upstream::ClusterManager& cluster_manager,
               Stats::Scope& scope, ThreadLocal::SlotAllocator& thread_local_slot_allocator)
    : tracer_stats_(makeTracerStats(scope)),
      thread_local_slot_(thread_local_slot_allocator.allocateSlot()) {
  const bool allow_added_via_api = true;
  Config::Utility::checkCluster("envoy.tracers.datadog", collector_cluster, cluster_manager,
                                allow_added_via_api);

  thread_local_slot_->set([&logger = ENVOY_LOGGER(), collector_cluster, collector_reference_host,
                           config, &tracer_stats = tracer_stats_,
                           &cluster_manager](Event::Dispatcher& dispatcher) {
    return makeThreadLocalTracer(config, cluster_manager, collector_cluster,
                                 collector_reference_host, tracer_stats, dispatcher, logger);
  });
}

// Tracer::TracingDriver

Tracing::SpanPtr Tracer::startSpan(const Tracing::Config&, Tracing::TraceContext& trace_context,
                                   const std::string& operation_name, SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  auto& thread_local_tracer = thread_local_slot_->getTyped<ThreadLocalTracer>();
  if (!thread_local_tracer.tracer) {
    return std::make_unique<Tracing::NullSpan>();
  }

  // The OpenTracing implementation ignored the `Tracing::Config` argument,
  // so we will as well.
  dd::SpanConfig span_config;
  span_config.name = operation_name;
  span_config.start = estimateTime(start_time);

  dd::Tracer& tracer = *thread_local_tracer.tracer;
  TraceContextReader reader{trace_context};
  auto maybe_span = tracer.extract_span(reader, span_config);
  if (auto* error = maybe_span.if_error()) {
    // We didn't extract a span. Either there's no span to extract, or an
    // error occurred during extraction.
    //
    // Either way, we're going to create a new root span, but if an error
    // occurred we're going to log the error.
    if (error->code != dd::Error::NO_SPAN_TO_EXTRACT) {
      ENVOY_LOG(
          error,
          "Unable to extract span context. Creating a new trace instead. Error [error {}]: {}",
          int(error->code), error->message);
    }

    maybe_span = tracer.create_span(span_config);
  }

  assert(maybe_span);
  dd::Span& span = *maybe_span;

  // If Envoy is telling us to drop the trace, then we treat that as a
  // "user drop" (manual override).
  //
  // If Envoy is telling us to keep the trace, then we leave it up to the
  // tracer's internal sampler (which might decide to drop the trace anyway).
  if (!tracing_decision.traced) {
    span.trace_segment().override_sampling_priority(int(dd::SamplingPriority::USER_DROP));
  }

  return std::make_unique<Span>(std::move(span));
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

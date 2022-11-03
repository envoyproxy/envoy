#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"

#include <string>

#include "envoy/config/trace/v3/opentelemetry.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/tracing/http_tracer_impl.h"

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/proto/trace/v1/trace.pb.h"
#include "span_context.h"
#include "span_context_extractor.h"
#include "tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

Driver::Driver(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
               Server::Configuration::TracerFactoryContext& context)
    : tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()),
      tracing_stats_{OPENTELEMETRY_TRACER_STATS(
          POOL_COUNTER_PREFIX(context.serverFactoryContext().scope(), "tracing.opentelemetry"))} {
  auto& factory_context = context.serverFactoryContext();
  // Create the tracer in Thread Local Storage.
  tls_slot_ptr_->set([opentelemetry_config, &factory_context, this](Event::Dispatcher& dispatcher) {
    Grpc::AsyncClientFactoryPtr&& factory =
        factory_context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            opentelemetry_config.grpc_service(), factory_context.scope(), true);
    const Grpc::RawAsyncClientSharedPtr& async_client_shared_ptr =
        factory->createUncachedRawAsyncClient();
    TracerPtr tracer = std::make_unique<Tracer>(
        std::make_unique<OpenTelemetryGrpcTraceExporter>(async_client_shared_ptr),
        factory_context.timeSource(), factory_context.api().randomGenerator(),
        factory_context.runtime(), dispatcher, tracing_stats_, opentelemetry_config.service_name());

    return std::make_shared<TlsTracer>(std::move(tracer));
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Tracing::TraceContext& trace_context,
                                   const std::string& operation_name, SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  // Get tracer from TLS and start span.
  auto& tracer = tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer();
  SpanContextExtractor extractor(trace_context);
  if (!extractor.propagationHeaderPresent()) {
    // No propagation header, so we can create a fresh span with the given decision.
    Tracing::SpanPtr new_open_telemetry_span =
        tracer.startSpan(config, operation_name, start_time, tracing_decision);
    new_open_telemetry_span->setSampled(tracing_decision.traced);
    return new_open_telemetry_span;
  } else {
    // Try to extract the span context. If we can't, just return a null span.
    absl::StatusOr<SpanContext> span_context = extractor.extractSpanContext();
    if (span_context.ok()) {
      return tracer.startSpan(config, operation_name, start_time, span_context.value());
    } else {
      ENVOY_LOG(trace, "Unable to extract span context: ", span_context.status());
      return std::make_unique<Tracing::NullSpan>();
    }
  }
}

Driver::TlsTracer::TlsTracer(TracerPtr tracer) : tracer_(std::move(tracer)) {}

Tracer& Driver::TlsTracer::tracer() {
  ASSERT(tracer_);
  return *tracer_;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

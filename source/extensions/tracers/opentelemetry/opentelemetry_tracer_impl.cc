#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"

#include <string>

#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/common/empty_string.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/proto/trace/v1/trace.pb.h"
#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "span_context.h"
#include "tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

const Http::LowerCaseString& openTelemetryPropagationHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "traceparent");
}
} // namespace

Driver::Driver(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
               Server::Configuration::TracerFactoryContext& context)
    : tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()) {
  auto& factory_context = context.serverFactoryContext();
  // Create the tracer in Thread Local Storage.
  tls_slot_ptr_->set(
      [opentelemetry_config, &factory_context /*, this*/](Event::Dispatcher& dispatcher) {
        Grpc::AsyncClientFactoryPtr&& factory =
            factory_context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
                opentelemetry_config.grpc_service(), factory_context.scope(), true);
        const Grpc::RawAsyncClientSharedPtr& async_client_shared_ptr =
            factory->createUncachedRawAsyncClient();
        TracerPtr tracer = std::make_unique<Tracer>(
            std::make_unique<OpenTelemetryGrpcTraceExporter>(async_client_shared_ptr,
                                                             opentelemetry_config.trace_name()),
            factory_context.timeSource(), factory_context.api().randomGenerator(),
            factory_context.runtime());

        return std::make_shared<TlsTracer>(std::move(tracer));
      });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Tracing::TraceContext& trace_context,
                                   const std::string& operation_name, SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  // Get tracer from TLS and start span.
  auto& tracer = tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer();
  auto propagation_header = trace_context.getByKey(openTelemetryPropagationHeader());
  if (!propagation_header.has_value()) {
    // No propagation header, so we can create a fresh span.
    return tracer.startSpan(config, operation_name, start_time, tracing_decision);
  } else {
    auto header_value_string = propagation_header.value();
    // Try to split it into its component parts:
    std::vector<std::string> propagation_header_components =
        absl::StrSplit(header_value_string, '-', absl::SkipEmpty());
    if (propagation_header_components.size() == 4) {
      // TODO: should we try to parse these to ensure they're valid?
      std::string version = propagation_header_components[0];
      std::string trace_id = propagation_header_components[1];
      std::string parent_id = propagation_header_components[2];
      std::string trace_flags = propagation_header_components[3];
      SpanContext span_context(version, trace_id, parent_id, trace_flags);
      return tracer.startSpan(config, operation_name, start_time, tracing_decision,
                              span_context);
    } else {
      // Something went wrong when parsing the propagation header.
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
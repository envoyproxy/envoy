#include "extensions/tracers/skywalking/skywalking_tracer_impl.h"

#include <memory>

#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/http/path_utility.h"

#include "extensions/tracers/skywalking/skywalking_types.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

Driver::Driver(const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
               Server::Configuration::TracerFactoryContext& context)
    : tracing_stats_{SKYWALKING_TRACER_STATS(
          POOL_COUNTER_PREFIX(context.serverFactoryContext().scope(), "tracing.skywalking."))},
      client_config_(
          std::make_unique<SkyWalkingClientConfig>(context, proto_config.client_config())),
      random_generator_(context.serverFactoryContext().api().randomGenerator()),
      tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()) {

  auto& factory_context = context.serverFactoryContext();
  tls_slot_ptr_->set([proto_config, &factory_context, this](Event::Dispatcher& dispatcher) {
    TracerPtr tracer = std::make_unique<Tracer>(factory_context.timeSource());
    tracer->setReporter(std::make_unique<TraceSegmentReporter>(
        factory_context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            proto_config.grpc_service(), factory_context.scope(), false),
        dispatcher, factory_context.api().randomGenerator(), tracing_stats_, *client_config_));
    return std::make_shared<TlsTracer>(std::move(tracer));
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Http::RequestHeaderMap& request_headers,
                                   const std::string& operation_name, Envoy::SystemTime start_time,
                                   const Tracing::Decision decision) {
  auto& tracer = *tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer_;

  try {
    SpanContextPtr previous_span_context = SpanContext::spanContextFromRequest(request_headers);
    auto segment_context = std::make_shared<SegmentContext>(std::move(previous_span_context),
                                                            decision, random_generator_);

    // Initialize fields of current span context.
    segment_context->setService(client_config_->service());
    segment_context->setServiceInstance(client_config_->serviceInstance());

    return tracer.startSpan(config, start_time, operation_name, std::move(segment_context),
                            nullptr);

  } catch (const EnvoyException& e) {
    ENVOY_LOG(warn, "New SkyWalking Span/Segment cannot be created for error: {}", e.what());
    return std::make_unique<Tracing::NullSpan>();
  }
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

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
      client_config_(proto_config.client_config()),
      random_generator_(context.serverFactoryContext().random()),
      tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()) {

  auto& factory_context = context.serverFactoryContext();

  if (client_config_.service_name().empty()) {
    client_config_.set_service_name(factory_context.localInfo().clusterName());
  }
  if (client_config_.instance_name().empty()) {
    client_config_.set_instance_name(factory_context.localInfo().nodeName());
  }

  tls_slot_ptr_->set([proto_config, &factory_context, this](
                         Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    TracerPtr tracer = std::make_unique<Tracer>(factory_context.timeSource());
    tracer->setReporter(std::make_unique<TraceSegmentReporter>(
        factory_context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            proto_config.grpc_service(), factory_context.scope(), false),
        dispatcher, factory_context.random(), tracing_stats_, client_config_));
    return std::make_shared<TlsTracer>(std::move(tracer));
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Http::RequestHeaderMap& request_headers, const std::string&,
                                   Envoy::SystemTime start_time, const Tracing::Decision decision) {
  auto& tracer = *tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer_;

  try {
    SpanContextPtr previous_span_context = SpanContext::spanContextFromRequest(request_headers);
    auto segment_context = std::make_shared<SegmentContext>(std::move(previous_span_context),
                                                            decision, random_generator_);

    // Initialize fields of current span context.
    segment_context->setService(client_config_.service_name());
    segment_context->setServiceInstance(client_config_.instance_name());

    // In order to be consistent with the existing SkyWalking agent, we use request Method and
    // request Path to create an operation name.
    // TODO(wbpcode): A temporary decision. This strategy still needs further consideration.
    std::string operation_name =
        absl::StrCat("/", request_headers.getMethodValue(),
                     Http::PathUtil::removeQueryAndFragment(request_headers.getPathValue()));

    if (!client_config_.pass_endpoint() || !segment_context->previousSpanContext()) {
      segment_context->setEndpoint(operation_name);
    } else {
      segment_context->setEndpoint(segment_context->previousSpanContext()->endpoint_);
    }

    return tracer.startSpan(config, start_time, operation_name, std::move(segment_context),
                            nullptr);

  } catch (const EnvoyException&) {
    return std::make_unique<Tracing::NullSpan>();
  }
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

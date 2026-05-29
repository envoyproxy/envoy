#include "source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/config.h"

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.pb.h"
#include "envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.pb.validate.h"
#include "envoy/server/tracer_config.h"

#include "source/common/config/utility.h"
#include "source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/trace_id_ratio_based_sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerSharedPtr TraceIdRatioBasedSamplerFactory::createSampler(
    const Protobuf::Message& config, Server::Configuration::TracerFactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const Protobuf::Any&>(config), context.messageValidationVisitor(), *this);

  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::tracers::opentelemetry::samplers::
                                           v3::TraceIdRatioBasedSamplerConfig&>(
          *mptr, context.messageValidationVisitor());

  return std::make_shared<TraceIdRatioBasedSampler>(proto_config, context);
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(TraceIdRatioBasedSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

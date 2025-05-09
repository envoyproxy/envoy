#include "source/extensions/tracers/opentelemetry/samplers/parent_based/config.h"

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/parent_based_sampler.pb.validate.h"
#include "envoy/server/tracer_config.h"

#include "source/common/config/utility.h"
#include "source/extensions/tracers/opentelemetry/samplers/parent_based/parent_based_sampler.h"
#include "source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/config.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerSharedPtr
ParentBasedSamplerFactory::createSampler(const Protobuf::Message& config,
                                         Server::Configuration::TracerFactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(config), context.messageValidationVisitor(), *this);
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::tracers::opentelemetry::samplers::v3::ParentBasedSamplerConfig&>(
      *mptr, context.messageValidationVisitor());
  auto& factory =
      Envoy::Config::Utility::getAndCheckFactory<SamplerFactory>(proto_config.wrapped_sampler());
  SamplerSharedPtr wrapped_sampler =
      factory.createSampler(proto_config.wrapped_sampler().typed_config(), context);
  return std::make_shared<ParentBasedSampler>(config, context, wrapped_sampler);
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(ParentBasedSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

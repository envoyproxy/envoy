#include "config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

#include "dynatrace_sampler.h"

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/dynatrace_sampler.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerPtr
DynatraceSamplerFactory::createSampler(const Protobuf::Message& config,
                                       Server::Configuration::TracerFactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(config), context.messageValidationVisitor(), *this);
  return std::make_shared<DynatraceSampler>(
      MessageUtil::downcastAndValidate<
          const envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig&>(
          *mptr, context.messageValidationVisitor()));
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(DynatraceSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
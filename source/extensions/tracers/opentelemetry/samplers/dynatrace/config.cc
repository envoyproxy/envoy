#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/config.h"

#include <memory>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/dynatrace_sampler.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/dynatrace_sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerSharedPtr
DynatraceSamplerFactory::createSampler(const Protobuf::Message& config,
                                       Server::Configuration::TracerFactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(config), context.messageValidationVisitor(), *this);

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig&>(
      *mptr, context.messageValidationVisitor());

  SamplerConfigProviderPtr cf = std::make_unique<SamplerConfigProviderImpl>(context, proto_config);
  return std::make_shared<DynatraceSampler>(proto_config, context, std::move(cf));
}

/**
 * Static registration for the Dynatrace sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(DynatraceSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/tracers/opentelemetry/samplers/cel/config.h"

#include <memory>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/cel_sampler.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/tracers/opentelemetry/samplers/cel/cel_sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerSharedPtr
CELSamplerFactory::createSampler(const Protobuf::Message& config,
                                 Server::Configuration::TracerFactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const Protobuf::Any&>(config), context.messageValidationVisitor(), *this);

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::tracers::opentelemetry::samplers::v3::CELSamplerConfig&>(
      *mptr, context.messageValidationVisitor());

  return std::make_unique<CELSampler>(
      context.serverFactoryContext().localInfo(),
      Extensions::Filters::Common::Expr::getBuilder(context.serverFactoryContext()),
      proto_config.expression());
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(CELSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

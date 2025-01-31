#include "source/extensions/tracers/opentelemetry/samplers/cel/config.h"

#include <memory>

#include "envoy/extensions/tracers/opentelemetry/samplers/v3/cel_sampler.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/tracers/opentelemetry/samplers/cel/cel_sampler.h"

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerSharedPtr
CELSamplerFactory::createSampler(const Protobuf::Message& config,
                                 Server::Configuration::TracerFactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(config), context.messageValidationVisitor(), *this);

#if defined(USE_CEL_PARSER)
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::tracers::opentelemetry::samplers::v3::CELSamplerConfig&>(
      *mptr, context.messageValidationVisitor());

  auto parse_status = google::api::expr::parser::Parse(proto_config.expression());
  if (!parse_status.ok()) {
    throw EnvoyException("Not able to parse cel expression: " + parse_status.status().ToString());
  }

  return std::make_unique<CELSampler>(
      context.serverFactoryContext().localInfo(),
      Extensions::Filters::Common::Expr::getBuilder(context.serverFactoryContext()),
      parse_status.value().expr());
#else
  throw EnvoyException("CEL is not available for use in this environment.");
#endif
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(CELSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

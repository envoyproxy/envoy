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

using ::xds::type::v3::CelExpression;

SamplerSharedPtr
CELSamplerFactory::createSampler(const Protobuf::Message& config,
                                 Server::Configuration::TracerFactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(config), context.messageValidationVisitor(), *this);

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::tracers::opentelemetry::samplers::v3::CELSamplerConfig&>(
      *mptr, context.messageValidationVisitor());

  const CelExpression& input_expr = proto_config.expression();
  google::api::expr::v1alpha1::Expr compiled_expr_;
  switch (input_expr.expr_specifier_case()) {
  case CelExpression::ExprSpecifierCase::kParsedExpr:
    compiled_expr_ = input_expr.parsed_expr().expr();
    break;
  case CelExpression::ExprSpecifierCase::kCheckedExpr:
    compiled_expr_ = input_expr.checked_expr().expr();
    break;
  case CelExpression::ExprSpecifierCase::EXPR_SPECIFIER_NOT_SET:
    throw EnvoyException("CEL expression not set");
  }

  return std::make_unique<CELSampler>(
      context.serverFactoryContext().localInfo(),
      Extensions::Filters::Common::Expr::getBuilder(context.serverFactoryContext()),
      compiled_expr_);
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(CELSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

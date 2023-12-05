#include "source/extensions/access_loggers/filters/cel/config.h"

#include "envoy/extensions/access_loggers/filters/cel/v3/cel.pb.h"

#include "source/extensions/access_loggers/filters/cel/cel.h"

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace CEL {

Envoy::AccessLog::FilterPtr CELAccessLogExtensionFilterFactory::createFilter(
    const envoy::config::accesslog::v3::ExtensionFilter& config,
    Server::Configuration::FactoryContext& context) {

  auto factory_config =
      Config::Utility::translateToFactoryConfig(config, context.messageValidationVisitor(), *this);

#if defined(USE_CEL_PARSER)
  envoy::extensions::access_loggers::filters::cel::v3::ExpressionFilter cel_config =
      *dynamic_cast<const envoy::extensions::access_loggers::filters::cel::v3::ExpressionFilter*>(
          factory_config.get());

  auto parse_status = google::api::expr::parser::Parse(cel_config.expression());
  if (!parse_status.ok()) {
    throw EnvoyException("Not able to parse filter expression: " +
                         parse_status.status().ToString());
  }

  return std::make_unique<CELAccessLogExtensionFilter>(
      Extensions::Filters::Common::Expr::getBuilder(context.serverFactoryContext()),
      parse_status.value().expr());
#else
  throw EnvoyException("CEL is not available for use in this environment.");
#endif
}

ProtobufTypes::MessagePtr CELAccessLogExtensionFilterFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::access_loggers::filters::cel::v3::ExpressionFilter>();
}

/**
 * Static registration for the CELAccessLogExtensionFilter. @see RegisterFactory.
 */
REGISTER_FACTORY(CELAccessLogExtensionFilterFactory, Envoy::AccessLog::ExtensionFilterFactory);

} // namespace CEL
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy

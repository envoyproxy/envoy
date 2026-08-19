#include "source/extensions/formatter/cel/config.h"

#include "envoy/extensions/formatter/cel/v3/cel.pb.h"
#include "envoy/extensions/formatter/cel/v3/cel.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/formatter/cel/cel.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

::Envoy::Formatter::CommandParserPtr CELFormatterFactory::createCommandParserFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::GenericFactoryContext& context) {
#if defined(USE_CEL_PARSER)
  const auto& config =
      MessageUtil::downcastAndValidate<const envoy::extensions::formatter::cel::v3::Cel&>(
          proto_config, context.messageValidationVisitor());
  const auto config_ref = config.has_cel_config()
                              ? Envoy::makeOptRef(config.cel_config())
                              : Envoy::OptRef<const envoy::config::core::v3::CelExpressionConfig>{};
  return std::make_unique<CELFormatterCommandParser>(
      context.serverFactoryContext().localInfo(),
      Filters::Common::Expr::getBuilder(context.serverFactoryContext(), config_ref));
#else
  UNREFERENCED_PARAMETER(proto_config);
  UNREFERENCED_PARAMETER(context);
  throw EnvoyException("CEL is not available for use in this environment.");
#endif
}

ProtobufTypes::MessagePtr CELFormatterFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::formatter::cel::v3::Cel>();
}

std::string CELFormatterFactory::name() const { return "envoy.formatter.cel"; }

REGISTER_FACTORY(CELFormatterFactory, ::Envoy::Formatter::CommandParserFactory);

class BuiltInCELFormatterFactory : public Envoy::Formatter::BuiltInCommandParserFactory {
public:
  std::string name() const override { return "envoy.built_in_formatters.cel"; }

  ::Envoy::Formatter::CommandParserPtr createCommandParser() const override {
    return std::make_unique<CELFormatterCommandParser>();
  }
};

REGISTER_FACTORY(BuiltInCELFormatterFactory, Envoy::Formatter::BuiltInCommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/formatter/cel/config.h"

#include "envoy/extensions/formatter/cel/v3/cel.pb.h"

#include "source/extensions/formatter/cel/cel.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

::Envoy::Formatter::CommandParserPtr
CELFormatterFactory::createCommandParserFromProto(const Protobuf::Message&,
                                                  Server::Configuration::GenericFactoryContext&) {
#if defined(USE_CEL_PARSER)
  ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::config), warn,
                      "'CEL' formatter is treated as a built-in formatter and does not "
                      "require configuration.");
  return std::make_unique<CELFormatterCommandParser>();
#else
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

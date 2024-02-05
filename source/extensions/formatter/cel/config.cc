#include "source/extensions/formatter/cel/config.h"

#include "envoy/extensions/formatter/cel/v3/cel.pb.h"

#include "source/extensions/formatter/cel/cel.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

::Envoy::Formatter::CommandParserPtr CELFormatterFactory::createCommandParserFromProto(
    const Protobuf::Message&, Server::Configuration::GenericFactoryContext& context) {
#if defined(USE_CEL_PARSER)
  return std::make_unique<CELFormatterCommandParser>(context.serverFactoryContext());
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

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy

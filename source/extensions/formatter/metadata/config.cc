#include "source/extensions/formatter/metadata/config.h"

#include "envoy/extensions/formatter/metadata/v3/metadata.pb.h"

#include "source/extensions/formatter/metadata/metadata.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

::Envoy::Formatter::CommandParserPtr MetadataFormatterFactory::createCommandParserFromProto(
    const Protobuf::Message&, Server::Configuration::GenericFactoryContext&) {
  ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::config), warn,
                      "'METADATA' formatter is treated as a built-in formatter and does not "
                      "require configuration.");
  return std::make_unique<MetadataFormatterCommandParser>();
}

ProtobufTypes::MessagePtr MetadataFormatterFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::formatter::metadata::v3::Metadata>();
}

std::string MetadataFormatterFactory::name() const { return "envoy.formatter.metadata"; }

REGISTER_FACTORY(MetadataFormatterFactory, ::Envoy::Formatter::CommandParserFactory);

class BuiltInMetadataFormatterFactory : public Envoy::Formatter::BuiltInCommandParserFactory {
public:
  std::string name() const override { return "envoy.built_in_formatters.metadata"; }

  ::Envoy::Formatter::CommandParserPtr createCommandParser() const override {
    return std::make_unique<MetadataFormatterCommandParser>();
  }
};

REGISTER_FACTORY(BuiltInMetadataFormatterFactory, Envoy::Formatter::BuiltInCommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy

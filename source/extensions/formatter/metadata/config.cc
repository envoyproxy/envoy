#include "source/extensions/formatter/metadata/config.h"

#include "envoy/extensions/formatter/metadata/v3/metadata.pb.h"

#include "source/extensions/formatter/metadata/metadata.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

::Envoy::Formatter::CommandParserPtr MetadataFormatterFactory::createCommandParserFromProto(
    const Protobuf::Message&, Server::Configuration::GenericFactoryContext&) {
  return std::make_unique<MetadataFormatterCommandParser>();
}

ProtobufTypes::MessagePtr MetadataFormatterFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::formatter::metadata::v3::Metadata>();
}

std::string MetadataFormatterFactory::name() const { return "envoy.formatter.metadata"; }

REGISTER_FACTORY(MetadataFormatterFactory, ::Envoy::Formatter::CommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy

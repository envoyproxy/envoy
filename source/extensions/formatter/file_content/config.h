#pragma once

#include "envoy/extensions/formatter/file_content/v3/file_content.pb.h"
#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

/**
 * CommandParserFactory for the %FILE_CONTENT(/path/to/file)% formatter.
 *
 * Reads the contents of the specified file and watches for changes on disk,
 * so that updates are automatically reflected in subsequent format calls.
 */
class FileContentFormatterFactory : public Envoy::Formatter::CommandParserFactory {
public:
  Envoy::Formatter::CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message& config,
                               Server::Configuration::GenericFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy

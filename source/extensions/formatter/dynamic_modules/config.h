#pragma once

#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace DynamicModules {

class DynamicModuleFormatterFactory : public ::Envoy::Formatter::CommandParserFactory {
public:
  ::Envoy::Formatter::CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message& config,
                               Server::Configuration::GenericFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

} // namespace DynamicModules
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy

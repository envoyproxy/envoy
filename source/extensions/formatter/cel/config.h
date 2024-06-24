#pragma once

#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/filters/common/expr/evaluator.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class CELFormatterFactory : public ::Envoy::Formatter::CommandParserFactory {
public:
  ::Envoy::Formatter::CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message&,
                               Server::Configuration::GenericFactoryContext&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy

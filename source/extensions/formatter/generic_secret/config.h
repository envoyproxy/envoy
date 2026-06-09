#pragma once

#include "envoy/extensions/formatter/generic_secret/v3/generic_secret.pb.h"
#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

/**
 * CommandParserFactory for the %SECRET% formatter.
 *
 * This formatter resolves the value of a generic secret (from SDS or static bootstrap config)
 * and makes it available as a substitution format command.
 */
class GenericSecretFormatterFactory : public Envoy::Formatter::CommandParserFactory {
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

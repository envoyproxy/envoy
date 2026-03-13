#pragma once

#include "envoy/extensions/formatter/datasource/v3/datasource.pb.h"
#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

/**
 * CommandParserFactory for the %DATASOURCE(name)% formatter.
 *
 * Resolves the current value of a named DataSource. File-based DataSources are
 * automatically re-read when the file is modified on disk, so that updates are
 * reflected in subsequent format calls without restarting Envoy.
 */
class DataSourceFormatterFactory : public Envoy::Formatter::CommandParserFactory {
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

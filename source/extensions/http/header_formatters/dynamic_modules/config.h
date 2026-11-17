#pragma once

#include "envoy/extensions/http/header_formatters/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/http/header_formatters/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/http/header_formatter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/http/header_formatters/dynamic_modules/header_formatter.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace DynamicModules {

class DynamicModuleHeaderFormatterFactoryConfig
    : public Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig {
public:
  // Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig
  std::string name() const override {
    return "envoy.http.stateful_header_formatters.dynamic_modules";
  }

  absl::StatusOr<Envoy::Http::StatefulHeaderKeyFormatterFactorySharedPtr>
  createFactoryFromProto(const Protobuf::Message& message,
                         Server::Configuration::GenericFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::header_formatters::dynamic_modules::v3::
                                DynamicModuleHeaderFormatter>();
  }
};

DECLARE_FACTORY(DynamicModuleHeaderFormatterFactoryConfig);

} // namespace DynamicModules
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy

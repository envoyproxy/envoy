#pragma once

#include "envoy/extensions/http/early_header_mutation/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/http/early_header_mutation/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/http/early_header_mutation.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/http/early_header_mutation/dynamic_modules/early_header_mutation.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace DynamicModules {

class Factory : public Envoy::Http::EarlyHeaderMutationFactory {
public:
  std::string name() const override { return "envoy.http.early_header_mutation.dynamic_modules"; }

  Envoy::Http::EarlyHeaderMutationPtr
  createExtension(const Protobuf::Message& config,
                  Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::early_header_mutation::dynamic_modules::v3::
                                DynamicModuleEarlyHeaderMutation>();
  }
};

} // namespace DynamicModules
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy

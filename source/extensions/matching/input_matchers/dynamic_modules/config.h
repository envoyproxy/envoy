#pragma once

#include "envoy/extensions/matching/input_matchers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/matching/input_matchers/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/matching/input_matchers/dynamic_modules/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {

class DynamicModuleInputMatcherFactory : public ::Envoy::Matcher::InputMatcherFactory {
public:
  ::Envoy::Matcher::InputMatcherFactoryCb
  createInputMatcherFactoryCb(const Protobuf::Message& config,
                              Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::input_matchers::dynamic_modules::v3::DynamicModuleMatcher>();
  }

  std::string name() const override { return "envoy.matching.matchers.dynamic_modules"; }
};

} // namespace DynamicModules
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

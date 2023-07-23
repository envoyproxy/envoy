#pragma once

#include "envoy/extensions/matching/input_matchers/runtime_fraction/v3/runtime_fraction.pb.h"
#include "envoy/extensions/matching/input_matchers/runtime_fraction/v3/runtime_fraction.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/matching/input_matchers/runtime_fraction/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace RuntimeFraction {

class Config : public Envoy::Matcher::InputMatcherFactory {
public:
  Envoy::Matcher::InputMatcherFactoryCb createInputMatcherFactoryCb(
      const Protobuf::Message& config,
      Server::Configuration::ServerFactoryContext& factory_context) override;

  std::string name() const override { return "envoy.matching.matchers.runtime_fraction"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::input_matchers::runtime_fraction::v3::RuntimeFraction>();
  }
};
} // namespace RuntimeFraction
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha/hyperscan.pb.h"
#include "contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha/hyperscan.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

class Config : public Envoy::Matcher::InputMatcherFactory {
public:
  std::string name() const override { return "envoy.matching.matchers.hyperscan"; }

  Envoy::Matcher::InputMatcherFactoryCb createInputMatcherFactoryCb(
      const Protobuf::Message& config,
      Server::Configuration::ServerFactoryContext& factory_context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan>();
  }
};

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

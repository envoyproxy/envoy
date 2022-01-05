#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha/hyperscan.pb.h"
#include "contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha/hyperscan.pb.validate.h"
#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"

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
      Server::Configuration::ServerFactoryContext& factory_context) override {
    const auto& hyperscan_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan&>(
        config, factory_context.messageValidationVisitor());

    return [hyperscan_config]() { return std::make_unique<Matcher>(hyperscan_config); };
  }
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

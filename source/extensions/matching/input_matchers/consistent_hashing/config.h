#pragma once

#include "envoy/extensions/matching/input_matchers/consistent_hashing/v3/consistent_hashing.pb.h"
#include "envoy/extensions/matching/input_matchers/consistent_hashing/v3/consistent_hashing.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/matching/input_matchers/consistent_hashing/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace ConsistentHashing {

class ConsistentHashingConfig : public Envoy::Matcher::InputMatcherFactory {
public:
  Envoy::Matcher::InputMatcherFactoryCb createInputMatcherFactoryCb(
      const Protobuf::Message& config,
      Server::Configuration::ServerFactoryContext& factory_context) override;

  std::string name() const override { return "envoy.matching.matchers.consistent_hashing"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::input_matchers::consistent_hashing::v3::ConsistentHashing>();
  }
};
} // namespace ConsistentHashing
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

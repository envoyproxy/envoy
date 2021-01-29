#pragma once

#include "envoy/extensions/matching/matchers/consistent_hashing/v3/consistent_hashing.pb.h"
#include "envoy/extensions/matching/matchers/consistent_hashing/v3/consistent_hashing.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "common/protobuf/utility.h"

#include "extensions/matching/matchers/consistent_hashing/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Matchers {
namespace ConsistentHashing {

class ConsistentHashingConfig : public Envoy::Matcher::InputMatcherFactory {
public:
  Envoy::Matcher::InputMatcherPtr
  createInputMatcher(const Protobuf::Message& config,
                     Server::Configuration::FactoryContext& factory_context) override {
    const auto& consistent_hashing_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::matching::matchers::consistent_hashing::v3::ConsistentHashing&>(
        config, factory_context.messageValidationVisitor());

    if (consistent_hashing_config.threshold() > consistent_hashing_config.modulo()) {
      throw EnvoyException(fmt::format("threshold cannot be greater than modulo: {} > {}",
                                       consistent_hashing_config.threshold(),
                                       consistent_hashing_config.modulo()));
    }

    return std::make_unique<Matcher>(consistent_hashing_config.threshold(),
                                     consistent_hashing_config.modulo());
  }

  std::string name() const override { return "envoy.matching.matchers.consistent_hashing"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::matchers::consistent_hashing::v3::ConsistentHashing>();
  }
};
} // namespace ConsistentHashing
} // namespace Matchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
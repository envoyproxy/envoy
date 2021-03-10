#pragma once

#include "envoy/extensions/matching/generic_inputs/environment/v3/environment.pb.h"
#include "envoy/extensions/matching/generic_inputs/environment/v3/environment.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "common/protobuf/utility.h"

#include "extensions/matching/generic_inputs/environment/input.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace GenericInputs {
namespace Environment {

class Config : public Envoy::Matcher::GenericDataInputFactory {
public:
  Envoy::Matcher::GenericDataInputPtr
  createGenericDataInput(const Protobuf::Message& config,
                         Server::Configuration::FactoryContext& factory_context) override;

  std::string name() const override { return "envoy.matching.generic_inputs.environment"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::generic_inputs::environment::v3::Environment>();
  }
};
} // namespace Environment
} // namespace GenericInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
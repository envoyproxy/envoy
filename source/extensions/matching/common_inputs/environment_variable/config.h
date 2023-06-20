#pragma once

#include "envoy/extensions/matching/common_inputs/environment_variable/v3/input.pb.h"
#include "envoy/extensions/matching/common_inputs/environment_variable/v3/input.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/matching/common_inputs/environment_variable/input.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
namespace EnvironmentVariable {

class Config : public Envoy::Matcher::CommonProtocolInputFactory {
public:
  Envoy::Matcher::CommonProtocolInputFactoryCb createCommonProtocolInputFactoryCb(
      const Protobuf::Message& config,
      ProtobufMessage::ValidationVisitor& validation_visitor) override;

  std::string name() const override { return "envoy.matching.common_inputs.environment_variable"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::environment_variable::v3::Config>();
  }
};
} // namespace EnvironmentVariable
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

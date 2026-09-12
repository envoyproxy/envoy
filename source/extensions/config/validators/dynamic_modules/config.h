#pragma once

#include "envoy/config/config_validator.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {
namespace DynamicModules {

class DynamicModuleConfigValidatorFactory : public Envoy::Config::ConfigValidatorFactory {
public:
  Envoy::Config::ConfigValidatorPtr
  createConfigValidator(const Protobuf::Any& config,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override;

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return "envoy.config.validators.dynamic_modules"; }

  std::string typeUrl() const override;

  std::string
  typeUrlFromConfig(const Protobuf::Any& config,
                    ProtobufMessage::ValidationVisitor& validation_visitor) const override;
};

} // namespace DynamicModules
} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy

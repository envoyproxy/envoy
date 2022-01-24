#pragma once

#include "envoy/config/config_validator.h"

#include "source/extensions/config/validators/minimum_clusters/minimum_clusters_validator.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {

class MinimumClustersValidatorFactory : public Envoy::Config::ConfigValidatorFactory {
public:
  MinimumClustersValidatorFactory() {}

  virtual Envoy::Config::ConfigValidatorPtr
  createConfigValidator(const ProtobufWkt::Any& config,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override;

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy

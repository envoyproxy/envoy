#include "source/extensions/config/validators/minimum_clusters/config.h"

#include "envoy/extensions/config/validators/minimum_clusters/v3/minimum_clusters.pb.h"
#include "envoy/extensions/config/validators/minimum_clusters/v3/minimum_clusters.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {

Envoy::Config::ConfigValidatorPtr MinimumClustersValidatorFactory::createConfigValidator(
    const ProtobufWkt::Any& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& validator_config = MessageUtil::anyConvertAndValidate<
      envoy::extensions::config::validators::minimum_clusters::v3::MinimumClustersValidator>(
      config, validation_visitor);

  return std::make_unique<MinimumClustersValidator>(validator_config);
}

Envoy::ProtobufTypes::MessagePtr MinimumClustersValidatorFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::config::validators::minimum_clusters::v3::MinimumClustersValidator>();
}

std::string MinimumClustersValidatorFactory::name() const {
  return absl::StrCat(category(), ".minimum_clusters_validator");
}

/**
 * Static registration for this config validator factory. @see RegisterFactory.
 */
REGISTER_FACTORY(MinimumClustersValidatorFactory,
                 Envoy::Config::ConfigValidatorFactory){"envoy.config.validators.minimum_clusters"};

} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy

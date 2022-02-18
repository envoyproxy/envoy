#pragma once

#include "envoy/config/config_validator.h"

namespace Envoy {
namespace Config {

// Represents a collection of external config validators for all xDS
// service type.
class ExternalConfigValidators {
public:
  virtual ~ExternalConfigValidators() = default;

  /**
   * Executes the validators that receive the State-of-the-World resources.
   *
   * @param type_url the xDS type url of the incoming resources.
   * @param resources the State-of-the-World resources from the new config
   *        update.
   * @throw EnvoyException if the config is rejected by one of the validators.
   */
  virtual void executeValidators(absl::string_view type_url,
                                 const std::vector<DecodedResourcePtr>& resources) PURE;

  /**
   * Executes the validators that receive the Incremental (delta-xDS) resources.
   *
   * @param type_url the xDS type url of the incoming resources.
   * @param added_resources the added/modified resources from the new config
   *        update.
   * @param removed_resources the resources to remove according to the new
   *        config update.
   * @throw EnvoyException if the config is rejected by one of the validators.
   */
  virtual void
  executeValidators(absl::string_view type_url,
                    const std::vector<DecodedResourcePtr>& added_resources,
                    const Protobuf::RepeatedPtrField<std::string>& removed_resources) PURE;
};

using ExternalConfigValidatorsPtr = std::unique_ptr<ExternalConfigValidators>;

} // namespace Config
} // namespace Envoy

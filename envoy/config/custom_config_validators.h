#pragma once

#include "envoy/config/subscription.h"

namespace Envoy {
namespace Config {

// Represents a collection of config validators defined using Envoy extensions,
// for all xDS service types. This is different than running Envoy in
// "validation-mode", as these config validators will be executed whenever
// Envoy receives a new update.
class CustomConfigValidators {
public:
  virtual ~CustomConfigValidators() = default;

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

using CustomConfigValidatorsPtr = std::unique_ptr<CustomConfigValidators>;

} // namespace Config
} // namespace Envoy

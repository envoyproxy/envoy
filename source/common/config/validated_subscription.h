#pragma once

#include "envoy/config/config_validator.h"
#include "envoy/server/instance.h"

namespace Envoy {
namespace Config {

// A mixin that implements the common xDS framework that executes the external
// validations checks for a subscription.
class ValidatedSubscription {
public:
  ValidatedSubscription(
      ProtobufMessage::ValidationVisitor& validation_visitor, Server::Instance& server,
      const Protobuf::RepeatedPtrField<
          envoy::config::core::v3::ConfigSource::ConfigSourceTypedConfig>& validators_configs);

protected:
  std::vector<Envoy::Config::ConfigValidatorPtr> validators_;
  Server::Instance& server_;

  /**
   * Executes the validators that receive the State-of-the-World resources.
   *
   * @param resources the State-of-the-World resources from the new config
   *        update.
   * @throw EnvoyException if the config is rejected by one of the validators.
   */
  void executeValidators(const std::vector<DecodedResourceRef>& resources);

  /**
   * Executes the validators that receive the Incremental (delta-xDS) resources.
   *
   * @param added_resources the added/modified resources from the new config
   *        update.
   * @param removed_resources the resources to remove according to the new
   *        config update.
   * @throw EnvoyException if the config is rejected by one of the validators.
   */
  void executeValidators(const std::vector<DecodedResourceRef>& added_resources,
                         const Protobuf::RepeatedPtrField<std::string>& removed_resources);
};

} // namespace Config
} // namespace Envoy

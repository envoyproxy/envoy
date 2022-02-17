#pragma once

#include "envoy/config/config_validator.h"
#include "envoy/server/instance.h"

namespace Envoy {
namespace Config {

// Represents a collection of external config validators for all xDS
// service type.
class ExternalConfigValidators {
public:
  ExternalConfigValidators(
      ProtobufMessage::ValidationVisitor& validation_visitor, Server::Instance& server,
      const Protobuf::RepeatedPtrField<
          envoy::config::core::v3::ApiConfigSource::ConfigSourceTypedConfig>& validators_configs);

  /**
   * Executes the validators that receive the State-of-the-World resources.
   *
   * @param resources the State-of-the-World resources from the new config
   *        update.
   * @throw EnvoyException if the config is rejected by one of the validators.
   */
  void executeValidators(absl::string_view type_url,
                         const std::vector<DecodedResourceRef>& resources);

  /**
   * Executes the validators that receive the Incremental (delta-xDS) resources.
   *
   * @param added_resources the added/modified resources from the new config
   *        update.
   * @param removed_resources the resources to remove according to the new
   *        config update.
   * @throw EnvoyException if the config is rejected by one of the validators.
   */
  void executeValidators(absl::string_view type_url,
                         const std::vector<DecodedResourceRef>& added_resources,
                         const Protobuf::RepeatedPtrField<std::string>& removed_resources);

private:
  absl::flat_hash_map<std::string, std::vector<Envoy::Config::ConfigValidatorPtr>> validators_map_;
  Server::Instance& server_;
};

using ExternalConfigValidatorsPtr = std::unique_ptr<ExternalConfigValidators>;

} // namespace Config
} // namespace Envoy

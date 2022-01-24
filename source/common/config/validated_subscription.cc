#include "source/common/config/validated_subscription.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/config/utility.h"

namespace Envoy {
namespace Config {

ValidatedSubscription::ValidatedSubscription(
    ProtobufMessage::ValidationVisitor& validation_visitor, Server::Instance& server,
    const Protobuf::RepeatedPtrField<
        envoy::config::core::v3::ConfigSource::ConfigSourceTypedConfig>& validators_configs)
    : server_(server) {
  validators_.reserve(validators_configs.size());
  for (const auto& validator_config : validators_configs) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Config::ConfigValidatorFactory>(validator_config);
    Config::ConfigValidatorPtr validator =
        factory.createConfigValidator(validator_config.typed_config(), validation_visitor);
    validators_.emplace_back(std::move(validator));
  }
}

void ValidatedSubscription::executeValidators(const std::vector<DecodedResourceRef>& resources) {
  for (auto& validator : validators_) {
    // A validator can either return false, or throw an EnvoyException.
    // Both will result in this method throwing an EnvoyException.
    if (!validator->validate(server_, resources)) {
      throw EnvoyException("External validator rejected the config.");
    }
  }
}

void ValidatedSubscription::executeValidators(
    const std::vector<DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  for (auto& validator : validators_) {
    // A validator can either return false, or throw an EnvoyException.
    // Both will result in this method throwing an EnvoyException.
    if (!validator->validate(server_, added_resources, removed_resources)) {
      throw EnvoyException("External validator rejected the config.");
    }
  }
}

} // namespace Config
} // namespace Envoy

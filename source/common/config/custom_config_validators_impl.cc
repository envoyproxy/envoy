#include "source/common/config/custom_config_validators_impl.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/config/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

CustomConfigValidatorsImpl::CustomConfigValidatorsImpl(
    ProtobufMessage::ValidationVisitor& validation_visitor, const Server::Instance& server,
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>&
        validators_configs)
    : server_(server) {
  for (const auto& validator_config : validators_configs) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Config::ConfigValidatorFactory>(validator_config);
    Config::ConfigValidatorPtr validator =
        factory.createConfigValidator(validator_config.typed_config(), validation_visitor);

    // Prefer the validator's own type url. Fall back to the deprecated factory-level type url for
    // validators that have not migrated to ConfigValidator::typeUrl(). Both this fallback and the
    // deprecated factory method will be removed once the migration is complete.
    absl::string_view type_url = validator->typeUrl();
    std::string factory_type_url;
    if (type_url.empty()) {
      factory_type_url = factory.typeUrl();
      type_url = factory_type_url;
    }

    // Insert a new vector for the type url if one doesn't exist.
    auto pair = validators_map_.emplace(type_url, 0);
    pair.first->second.emplace_back(std::move(validator));
  }
}

void CustomConfigValidatorsImpl::executeValidators(
    absl::string_view type_url, const std::vector<DecodedResourcePtr>& resources) {
  auto validators_it = validators_map_.find(type_url);
  if (validators_it != validators_map_.end()) {
    auto& validators = validators_it->second;
    for (auto& validator : validators) {
      // A validator can either succeed, or throw an EnvoyException.
      validator->validate(server_, resources);
    }
  }
}

void CustomConfigValidatorsImpl::executeValidators(
    absl::string_view type_url, const std::vector<DecodedResourcePtr>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  auto validators_it = validators_map_.find(type_url);
  if (validators_it != validators_map_.end()) {
    auto& validators = validators_it->second;
    for (auto& validator : validators) {
      // A validator can either succeed, or throw an EnvoyException.
      validator->validate(server_, added_resources, removed_resources);
    }
  }
}

} // namespace Config
} // namespace Envoy

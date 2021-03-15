#include "extensions/matching/generic_inputs/environment/config.h"

#include <memory>

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace GenericInputs {
namespace Environment {

Envoy::Matcher::GenericDataInputPtr
Config::createGenericDataInput(const Protobuf::Message& config,
                               Server::Configuration::FactoryContext& factory_context) {
  const auto& environment_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::generic_inputs::environment::v3::Environment&>(
      config, factory_context.messageValidationVisitor());

  // We read the env variable at construction time to avoid repeat lookups.
  // This assumes that the environment remains stable during the process lifetime.
  auto* value = getenv(environment_config.name().data());
  if (value != nullptr) {
    return std::make_unique<Input>(std::string(value));
  }

  return std::make_unique<Input>(absl::nullopt);
}

/**
 * Static registration for the environment data input. @see RegisterFactory.
 */
REGISTER_FACTORY(Config, Envoy::Matcher::GenericDataInputFactory);

} // namespace Environment
} // namespace GenericInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
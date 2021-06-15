#include "source/extensions/matching/common_inputs/environment_variable/config.h"

#include <memory>

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
namespace EnvironmentVariable {

Envoy::Matcher::CommonProtocolInputFactoryCb
Config::createCommonProtocolInputFactoryCb(const Protobuf::Message& config,
                                           Server::Configuration::FactoryContext& factory_context) {
  const auto& environment_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::common_inputs::environment_variable::v3::Config&>(
      config, factory_context.messageValidationVisitor());

  // We read the env variable at construction time to avoid repeat lookups.
  // This assumes that the environment remains stable during the process lifetime.
  auto* value = getenv(environment_config.name().data());
  if (value != nullptr) {
    return [s = std::string(value)]() { return std::make_unique<Input>(s); };
  }

  return []() { return std::make_unique<Input>(absl::nullopt); };
}

/**
 * Static registration for the environment data input. @see RegisterFactory.
 */
REGISTER_FACTORY(Config, Envoy::Matcher::CommonProtocolInputFactory);

} // namespace EnvironmentVariable
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

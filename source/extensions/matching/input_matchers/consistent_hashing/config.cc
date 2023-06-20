#include "source/extensions/matching/input_matchers/consistent_hashing/config.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace ConsistentHashing {

Envoy::Matcher::InputMatcherFactoryCb ConsistentHashingConfig::createInputMatcherFactoryCb(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& factory_context) {
  const auto& consistent_hashing_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::matching::input_matchers::
                                           consistent_hashing::v3::ConsistentHashing&>(
          config, factory_context.messageValidationVisitor());

  if (consistent_hashing_config.threshold() > consistent_hashing_config.modulo()) {
    throw EnvoyException(fmt::format("threshold cannot be greater than modulo: {} > {}",
                                     consistent_hashing_config.threshold(),
                                     consistent_hashing_config.modulo()));
  }

  return [consistent_hashing_config]() {
    return std::make_unique<Matcher>(consistent_hashing_config.threshold(),
                                     consistent_hashing_config.modulo(),
                                     consistent_hashing_config.seed());
  };
}
/**
 * Static registration for the consistent hashing matcher. @see RegisterFactory.
 */
REGISTER_FACTORY(ConsistentHashingConfig, Envoy::Matcher::InputMatcherFactory);

} // namespace ConsistentHashing
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

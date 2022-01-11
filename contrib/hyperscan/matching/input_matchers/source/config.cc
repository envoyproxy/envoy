#include "contrib/hyperscan/matching/input_matchers/source/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

Envoy::Matcher::InputMatcherFactoryCb
Config::createInputMatcherFactoryCb(const Protobuf::Message& config,
                                    Server::Configuration::ServerFactoryContext& factory_context) {
  const auto& hyperscan_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan&>(
      config, factory_context.messageValidationVisitor());

#ifdef HYPERSCAN_DISABLED
  throw EnvoyException("X86_64 architecture is required for Hyperscan.");
#else
  return [hyperscan_config]() { return std::make_unique<Matcher>(hyperscan_config); };
#endif
}

REGISTER_FACTORY(Config, Envoy::Matcher::InputMatcherFactory);

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/matching/input_matchers/runtime_fraction/config.h"

#include "envoy/extensions/matching/input_matchers/runtime_fraction/v3/runtime_fraction.pb.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace RuntimeFraction {

Envoy::Matcher::InputMatcherFactoryCb
Config::createInputMatcherFactoryCb(const Protobuf::Message& config,
                                    Server::Configuration::ServerFactoryContext& factory_context) {
  const auto& runtime_fraction_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::input_matchers::runtime_fraction::v3::RuntimeFraction&>(
      config, factory_context.messageValidationVisitor());

  auto& runtime = factory_context.runtime();
  return [runtime_fraction_config, &runtime]() {
    return std::make_unique<Matcher>(runtime, runtime_fraction_config.runtime_fraction(),
                                     runtime_fraction_config.seed());
  };
}
/**
 * Static registration for the runtime fraction matcher. @see RegisterFactory.
 */
REGISTER_FACTORY(Config, Envoy::Matcher::InputMatcherFactory);

} // namespace RuntimeFraction
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

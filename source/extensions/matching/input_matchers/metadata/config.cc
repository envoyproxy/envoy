#include "source/extensions/matching/input_matchers/metadata/config.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Metadata {

Envoy::Matcher::InputMatcherFactoryCb
Config::createInputMatcherFactoryCb(const Protobuf::Message& config,
                                    Server::Configuration::ServerFactoryContext& factory_context) {
  const auto& matcher_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::input_matchers::metadata::v3::Metadata&>(
      config, factory_context.messageValidationVisitor());
  const auto& value = matcher_config.value();
  const auto value_matcher = Envoy::Matchers::ValueMatcher::create(value, factory_context);
  const bool invert = matcher_config.invert();
  return [value_matcher, invert]() { return std::make_unique<Matcher>(value_matcher, invert); };
}
/**
 * Static registration for the Metadata matcher. @see RegisterFactory.
 */
REGISTER_FACTORY(Config, Envoy::Matcher::InputMatcherFactory);

} // namespace Metadata
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

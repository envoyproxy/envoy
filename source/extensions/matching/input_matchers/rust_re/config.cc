#include "source/extensions/matching/input_matchers/rust_re/config.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace RustRe {

Envoy::Matcher::InputMatcherFactoryCb RustReConfig::createInputMatcherFactoryCb(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& factory_context) {
  const auto& rust_re_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::input_matchers::rust_re::v3::RustRe&>(
      config, factory_context.messageValidationVisitor());

  return [rust_re_config]() { return std::make_unique<Matcher>(rust_re_config.regex()); };
}
/**
 * Static registration for the consistent hashing matcher. @see RegisterFactory.
 */
REGISTER_FACTORY(RustReConfig, Envoy::Matcher::InputMatcherFactory);

} // namespace RustRe
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/matching/input_matchers/ip/config.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

Envoy::Matcher::InputMatcherPtr
Config::createInputMatcher(const Protobuf::Message& config,
                           Server::Configuration::FactoryContext& context) {
  const auto& ip_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::input_matchers::ip::v3::Ip&>(
      config, context.messageValidationVisitor());

  const auto& cidr_ranges = ip_config.cidr_ranges();
  std::vector<Network::Address::CidrRange> ranges;
  ranges.reserve(cidr_ranges.size());
  for (const auto& cidr_range : cidr_ranges) {
    const std::string& address = cidr_range.address_prefix();
    const uint32_t prefix_len = cidr_range.prefix_len().value();
    const auto range = Network::Address::CidrRange::create(address, prefix_len);
    if (!range.isValid()) {
      throw EnvoyException(fmt::format("ip range {}/{} is invalid", address, prefix_len));
    }
    ranges.emplace_back(std::move(range));
  }

  const auto stat_prefix = ip_config.stat_prefix();
  return std::make_unique<Matcher>(std::move(ranges), stat_prefix, context.scope());
}
/**
 * Static registration for the consistent hashing matcher. @see RegisterFactory.
 */
REGISTER_FACTORY(Config, Envoy::Matcher::InputMatcherFactory);

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

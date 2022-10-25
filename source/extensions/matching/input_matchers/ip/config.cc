#include "source/extensions/matching/input_matchers/ip/config.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

Envoy::Matcher::InputMatcherFactoryCb
Config::createInputMatcherFactoryCb(const Protobuf::Message& config,
                                    Server::Configuration::ServerFactoryContext& factory_context) {
  const auto& ip_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::input_matchers::ip::v3::Ip&>(
      config, factory_context.messageValidationVisitor());

  const auto& cidr_ranges = ip_config.cidr_ranges();
  std::vector<Network::Address::CidrRange> ranges;
  ranges.reserve(cidr_ranges.size());
  for (const auto& cidr_range : cidr_ranges) {
    const std::string& address = cidr_range.address_prefix();
    const uint32_t prefix_len = cidr_range.prefix_len().value();
    const auto range = Network::Address::CidrRange::create(address, prefix_len);
    // We only assert that the range is valid because:
    // * if "address" can't be parsed, it will throw an EnvoyException
    // * prefix_len can't be < 0 as per the protobuf definition as an uint32_t
    // * if prefix_len is too big, CidrRange::create clamps it to a valid value
    // => it is thus not possible to create an invalid range.
    ASSERT(range.isValid(), "address range should be valid!");
    ranges.emplace_back(std::move(range));
  }

  const std::string& stat_prefix = ip_config.stat_prefix();
  Stats::Scope& scope = factory_context.scope();
  return [ranges, stat_prefix, &scope]() {
    return std::make_unique<Matcher>(ranges, stat_prefix, scope);
  };
}
/**
 * Static registration for the IP matcher. @see RegisterFactory.
 */
REGISTER_FACTORY(Config, Envoy::Matcher::InputMatcherFactory);

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

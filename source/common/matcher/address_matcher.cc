#include "source/common/matcher/address_matcher.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace Matcher {

absl::StatusOr<std::unique_ptr<AddressMatcher>>
AddressMatcher::create(const envoy::type::matcher::v3::AddressMatcher& matcher) {
  auto ip_list = Network::Address::IpList::create(matcher.ranges());
  RETURN_IF_NOT_OK_REF(ip_list.status());
  return std::make_unique<AddressMatcher>(std::move(*ip_list), matcher.invert_match());
}

AddressMatcher::AddressMatcher(std::unique_ptr<Network::Address::IpList>&& ip_list,
                               bool invert_match)
    : ip_list_(std::move(ip_list)), invert_match_(invert_match) {}

bool AddressMatcher::match(const Network::Address::Instance& address) const {
  const bool matches = ip_list_->contains(address);
  return invert_match_ ? !matches : matches;
}

} // namespace Matcher
} // namespace Envoy

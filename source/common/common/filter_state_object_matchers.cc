#include "source/common/common/filter_state_object_matchers.h"

#include "envoy/common/exception.h"
#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/network/cidr_range.h"

#include "absl/status/statusor.h"
#include "xds/core/v3/cidr.pb.h"

namespace Envoy {
namespace Matchers {

FilterStateIpRangeMatcher::FilterStateIpRangeMatcher(
    std::unique_ptr<Network::Address::IpList>&& ip_list)
    : ip_list_(std::move(ip_list)) {}

bool FilterStateIpRangeMatcher::match(const StreamInfo::FilterState::Object& object) const {
  const Network::Address::InstanceAccessor* ip =
      dynamic_cast<const Network::Address::InstanceAccessor*>(&object);
  if (ip == nullptr) {
    return false;
  }
  return ip_list_->contains(*ip->getIp());
}

FilterStateStringMatcher::FilterStateStringMatcher(StringMatcherPtr&& string_matcher)
    : string_matcher_(std::move(string_matcher)) {}

bool FilterStateStringMatcher::match(const StreamInfo::FilterState::Object& object) const {
  const auto string_value = object.serializeAsString();
  return string_value && string_matcher_->match(*string_value);
}

} // namespace Matchers
} // namespace Envoy

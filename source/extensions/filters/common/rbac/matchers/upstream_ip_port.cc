#include "source/extensions/filters/common/rbac/matchers/upstream_ip_port.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/stream_info/upstream_address.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matchers {

using namespace Filters::Common::RBAC;

UpstreamIpPortMatcher::UpstreamIpPortMatcher(
    const envoy::extensions::rbac::matchers::upstream_ip_port::v3::UpstreamIpPortMatcher& proto) {
  if (proto.has_upstream_ip()) {
    cidr_ = proto.upstream_ip();
  }
  if (proto.has_upstream_port_range()) {
    port_ = proto.upstream_port_range();
  }
}

bool UpstreamIpPortMatcher::matches(const Network::Connection&,
                                    const Envoy::Http::RequestHeaderMap&,
                                    const StreamInfo::StreamInfo& info) const {

  if (!info.filterState().hasDataWithName(StreamInfo::UpstreamAddress::key())) {
    ENVOY_LOG_EVERY_POW_2(
        warn,
        "Did not find filter state with key: {}. Do you have a filter in the filter chain "
        "before the RBAC filter which populates the filter state with upstream addresses ?",
        StreamInfo::UpstreamAddress::key());

    return false;
  }

  const StreamInfo::UpstreamAddress& address_obj =
      info.filterState().getDataReadOnly<StreamInfo::UpstreamAddress>(
          StreamInfo::UpstreamAddress::key());

  bool is_match = false;
  if (cidr_) {
    const auto range = Network::Address::CidrRange::create(*cidr_);
    if (range.isInRange(*address_obj.address_)) {
      ENVOY_LOG(debug, "UpstreamIpPort matcher for cidr range: {} evaluated to: true",
                range.asString());

      is_match = true;
    } else {
      ENVOY_LOG(debug, "UpstreamIpPort matcher for cidr range: {} evaluated to: false",
                range.asString());
      return false;
    }
  }

  if (port_) {
    const auto port = address_obj.address_->ip()->port();
    if (port >= port_->start() && port <= port_->end()) {
      ENVOY_LOG(debug, "UpstreamIpPort matcher for port range: {{}, {}} evaluated to: true",
                port_->start(), port_->end());
      is_match = true;
    } else {
      ENVOY_LOG(debug, "UpstreamIpPort matcher for port range: {{}, {}} evaluated to: false",
                port_->start(), port_->end());
      return false;
    }
  }

  ENVOY_LOG(trace, "UpstreamIpPort matcher evaluated to: {}", is_match);
  return is_match;
}

REGISTER_FACTORY(UpstreamIpPortMatcherFactory, MatcherExtensionFactory);

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy

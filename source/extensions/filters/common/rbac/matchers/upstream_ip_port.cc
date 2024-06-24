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
  if (!proto.has_upstream_ip() && !proto.has_upstream_port_range()) {
    throw EnvoyException(
        "Invalid UpstreamIpPortMatcher configuration - missing `upstream_ip` and/or"
        " `upstream_port_range`");
  }

  if (proto.has_upstream_ip()) {
    cidr_ = THROW_OR_RETURN_VALUE(Network::Address::CidrRange::create(proto.upstream_ip()),
                                  Network::Address::CidrRange);
  }
  if (proto.has_upstream_port_range()) {
    port_ = proto.upstream_port_range();
  }
}

bool UpstreamIpPortMatcher::matches(const Network::Connection&,
                                    const Envoy::Http::RequestHeaderMap&,
                                    const StreamInfo::StreamInfo& info) const {
  auto address_obj = info.filterState().getDataReadOnly<StreamInfo::UpstreamAddress>(
      StreamInfo::UpstreamAddress::key());
  if (address_obj == nullptr) {
    ENVOY_LOG_EVERY_POW_2(
        warn,
        "Did not find filter state with key: {}. Do you have a filter in the filter chain "
        "before the RBAC filter which populates the filter state with upstream addresses ?",
        StreamInfo::UpstreamAddress::key());

    return false;
  }

  if (cidr_) {
    if (cidr_->isInRange(*address_obj->address_)) {
      ENVOY_LOG(debug, "UpstreamIpPort matcher for cidr range: {} evaluated to: true",
                cidr_->asString());

    } else {
      ENVOY_LOG(debug, "UpstreamIpPort matcher for cidr range: {} evaluated to: false",
                cidr_->asString());
      return false;
    }
  }

  if (port_) {
    const auto port = address_obj->address_->ip()->port();
    if (port >= port_->start() && port <= port_->end()) {
      ENVOY_LOG(debug, "UpstreamIpPort matcher for port range: [{}, {}] evaluated to: true",
                port_->start(), port_->end());
    } else {
      ENVOY_LOG(debug, "UpstreamIpPort matcher for port range: [{}, {}] evaluated to: false",
                port_->start(), port_->end());
      return false;
    }
  }

  ENVOY_LOG(trace, "UpstreamIpPort matcher evaluated to: true");
  return true;
}

REGISTER_FACTORY(UpstreamIpPortMatcherFactory, MatcherExtensionFactory);

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy

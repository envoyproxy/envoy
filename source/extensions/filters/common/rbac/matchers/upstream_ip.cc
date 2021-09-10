#include "source/extensions/filters/common/rbac/matchers/upstream_ip.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/stream_info/upstream_address_set.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matchers {

using namespace Filters::Common::RBAC;

bool UpstreamIpMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                                const StreamInfo::StreamInfo& info) const {

  if (!info.filterState().hasDataWithName(StreamInfo::UpstreamAddressSet::key())) {
    ENVOY_LOG(warn,
              "Did not find filter state with key: {}. Do you have a filter in the filter chain "
              "before the RBAC filter which populates the filter state with upstream addresses ?",
              StreamInfo::UpstreamAddressSet::key());

    return false;
  }

  bool ipMatch = false;

  const StreamInfo::UpstreamAddressSet& address_set =
      info.filterState().getDataReadOnly<StreamInfo::UpstreamAddressSet>(
          StreamInfo::UpstreamAddressSet::key());

  address_set.iterate([&, this](const Network::Address::InstanceConstSharedPtr& address) {
    ipMatch = range_.isInRange(*address.get());
    if (ipMatch) {
      ENVOY_LOG(debug, "Address {} matched range: {}", address->asString(), range_.asString());
      return false;
    }

    return true;
  });

  ENVOY_LOG(debug, "UpstreamIp matcher for range: {} evaluated to: {}", range_.asString(), ipMatch);
  return ipMatch;
}

REGISTER_FACTORY(UpstreamIpMatcherFactory, MatcherExtensionFactory);

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy

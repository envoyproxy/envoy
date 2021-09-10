#include "source/extensions/filters/common/rbac/matchers/upstream_port.h"

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

bool UpstreamPortMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                                  const StreamInfo::StreamInfo& info) const {
  if (start_ > end_) {
    ENVOY_LOG(error,
              "Upstream port matcher is misconfigured. Port range start: {} is greater than port "
              "range end: {}",
              start_, end_);

    return false;
  }

  if (!info.filterState().hasDataWithName(StreamInfo::UpstreamAddressSet::key())) {
    ENVOY_LOG(warn,
              "Did not find filter state with key: {}. Do you have a filter in the filter chain "
              "before the RBAC filter which populates the filter state with upstream addresses ?",
              StreamInfo::UpstreamAddressSet::key());

    return false;
  }

  bool isMatch = false;

  const StreamInfo::UpstreamAddressSet& address_set =
      info.filterState().getDataReadOnly<StreamInfo::UpstreamAddressSet>(
          StreamInfo::UpstreamAddressSet::key());

  address_set.iterate([&, this](const Network::Address::InstanceConstSharedPtr& address) {
    auto port = address->ip()->port();

    isMatch = (port >= start_ && port <= end_);
    if (isMatch) {
      ENVOY_LOG(debug, "Port {} matched range: {}, {}", port, start_, end_);
      return false;
    }

    return true;
  });

  ENVOY_LOG(debug, "UpstreamPort matcher for range ({}, {}) evaluated to: {}", start_, end_,
            isMatch);
  return isMatch;
}

REGISTER_FACTORY(UpstreamPortMatcherFactory, MatcherExtensionFactory);

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy

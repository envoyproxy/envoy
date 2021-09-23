#include "source/extensions/filters/common/rbac/matchers/upstream_port.h"

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

bool UpstreamPortMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
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

  const auto port = address_obj.address_->ip()->port();
  if (port >= start_ && port <= end_) {
      ENVOY_LOG(debug, "Port {} matched range: {}, {}", port, start_, end_);
      return true;
  }

  ENVOY_LOG(trace, "Port {} did not match range: {}, {}", port, start_, end_);
  return false;
}

REGISTER_FACTORY(UpstreamPortMatcherFactory, MatcherExtensionFactory);

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/filters/common/rbac/matchers/upstream_port.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/stream_info/set_filter_state_object_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matchers {

using namespace Filters::Common::RBAC;

bool UpstreamPortMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                                  const StreamInfo::StreamInfo& info) const {

  using AddressSetFilterStateObjectImpl =
      StreamInfo::SetFilterStateObjectImpl<Network::Address::InstanceConstSharedPtr>;

  if (!info.filterState().hasDataWithName(AddressSetFilterStateObjectImpl::key())) {
    ENVOY_LOG_MISC(warn, "Did not find dynamic forward proxy metadata. Do you have dynamic "
                         "forward proxy in the filter chain before the RBAC filter ?");
    return false;
  }

  bool isMatch = false;

  const AddressSetFilterStateObjectImpl& address_set =
      info.filterState().getDataReadOnly<AddressSetFilterStateObjectImpl>(
          AddressSetFilterStateObjectImpl::key());

  address_set.iterate([&, this](const Network::Address::InstanceConstSharedPtr& address) {
    auto port = address->ip()->port();

    isMatch = (port >= start_ && port <= end_);
    if (isMatch) {
      ENVOY_LOG_MISC(debug, "Port {} matched range: {}, {}", port, start_, end_);
      return false;
    }

    return true;
  });

  ENVOY_LOG_MISC(debug, "UpstreamPort matcher for range ({}, {}) evaluated to: {}", start_, end_,
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

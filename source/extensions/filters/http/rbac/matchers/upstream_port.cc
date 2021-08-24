#include "source/extensions/filters/http/rbac/matchers/upstream_port.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/stream_info/address_set_accessor_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace Matcher {

using namespace Filters::Common::RBAC;

bool UpstreamPortMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                                  const StreamInfo::StreamInfo& info) const {

  if (!info.filterState().hasDataWithName(StreamInfo::AddressSetAccessorImpl::key())) {
    ENVOY_LOG_MISC(warn, "Did not find dynamic forward proxy metadata. Do you have dynamic "
                         "forward proxy in the filter chain before the RBAC filter ?");
    return false;
  }

  bool isMatch = false;

  const StreamInfo::AddressSetAccessor& address_set =
      info.filterState().getDataReadOnly<StreamInfo::AddressSetAccessor>(
          StreamInfo::AddressSetAccessorImpl::key());

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

} // namespace Matcher
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

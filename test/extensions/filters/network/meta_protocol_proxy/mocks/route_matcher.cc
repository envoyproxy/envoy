#include "test/extensions/filters/network/meta_protocol_proxy/mocks/route_matcher.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

MockRouteEntry::MockRouteEntry() : typed_metadata_(metadata_) {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, typedMetadata()).WillByDefault(ReturnRef(typed_metadata_));
  ON_CALL(*this, metadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(*this, retryPolicy()).WillByDefault(ReturnRef(retry_policy_));
}

MockRouteMatcher::MockRouteMatcher() {
  ON_CALL(*this, routeEntry(_)).WillByDefault(Return(route_entry_));
}

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

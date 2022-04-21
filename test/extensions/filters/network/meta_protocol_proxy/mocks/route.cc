#include "test/extensions/filters/network/meta_protocol_proxy/mocks/route.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, metadata()).WillByDefault(ReturnRef(metadata_));
}

MockRouteMatcher::MockRouteMatcher() {
  ON_CALL(*this, routeEntry(_)).WillByDefault(Return(route_entry_));
}

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

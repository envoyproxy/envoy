#include "contrib/generic_proxy/filters/network/test/mocks/route.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, metadata()).WillByDefault(ReturnRef(metadata_));
}

MockRouteMatcher::MockRouteMatcher() {
  ON_CALL(*this, routeEntry(_)).WillByDefault(Return(route_entry_));
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

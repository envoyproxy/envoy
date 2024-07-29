#include "test/extensions/filters/network/generic_proxy/mocks/route.h"

using testing::_;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, metadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(*this, timeout()).WillByDefault(ReturnPointee(&timeout_));
  ON_CALL(*this, retryPolicy()).WillByDefault(ReturnRef(retry_policy_));
}

MockRouteMatcher::MockRouteMatcher() {
  ON_CALL(*this, routeEntry(_)).WillByDefault(Return(route_entry_));
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

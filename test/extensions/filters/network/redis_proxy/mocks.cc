#include "mocks.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

MockRoute::MockRoute(ConnPool::InstanceSharedPtr conn_pool) : conn_pool_(std::move(conn_pool)) {
  ON_CALL(*this, upstream()).WillByDefault(Return(conn_pool_));
  ON_CALL(*this, mirrorPolicies()).WillByDefault(ReturnRef(policies_));
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

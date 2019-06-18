#include "mocks.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

MockRouter::MockRouter() {}
MockRouter::~MockRouter() {}

MockRoute::MockRoute(ConnPool::InstanceSharedPtr conn_pool) : conn_pool_(std::move(conn_pool)) {
  ON_CALL(*this, upstream()).WillByDefault(Return(conn_pool_));
  ON_CALL(*this, mirrorPolicies()).WillByDefault(ReturnRef(policies_));
}
MockRoute::~MockRoute() {}

namespace ConnPool {

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

} // namespace ConnPool

namespace CommandSplitter {

MockSplitRequest::MockSplitRequest() {}
MockSplitRequest::~MockSplitRequest() {}

MockSplitCallbacks::MockSplitCallbacks() {}
MockSplitCallbacks::~MockSplitCallbacks() {}

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

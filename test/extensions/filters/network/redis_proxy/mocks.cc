#include "mocks.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

MockRouter::MockRouter() = default;
MockRouter::~MockRouter() = default;

MockRoute::MockRoute(ConnPool::InstanceSharedPtr conn_pool) : conn_pool_(std::move(conn_pool)) {
  ON_CALL(*this, upstream()).WillByDefault(Return(conn_pool_));
  ON_CALL(*this, mirrorPolicies()).WillByDefault(ReturnRef(policies_));
}
MockRoute::~MockRoute() = default;

namespace ConnPool {

MockInstance::MockInstance() = default;
MockInstance::~MockInstance() = default;

} // namespace ConnPool

namespace CommandSplitter {

MockSplitRequest::MockSplitRequest() = default;
MockSplitRequest::~MockSplitRequest() = default;

MockSplitCallbacks::MockSplitCallbacks() = default;
MockSplitCallbacks::~MockSplitCallbacks() = default;

MockInstance::MockInstance() = default;
MockInstance::~MockInstance() = default;

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

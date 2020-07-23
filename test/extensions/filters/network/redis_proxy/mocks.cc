#include "mocks.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

MockRouter::MockRouter(RouteSharedPtr route) : route_(std::move(route)) {
  ON_CALL(*this, upstreamPool(_)).WillByDefault(Return(route_));
}
MockRouter::~MockRouter() = default;

MockRoute::MockRoute(ConnPool::InstanceSharedPtr conn_pool) : conn_pool_(std::move(conn_pool)) {
  ON_CALL(*this, upstream()).WillByDefault(Return(conn_pool_));
  ON_CALL(*this, mirrorPolicies()).WillByDefault(ReturnRef(policies_));
}
MockRoute::~MockRoute() = default;

MockMirrorPolicy::MockMirrorPolicy(ConnPool::InstanceSharedPtr conn_pool)
    : conn_pool_(std::move(conn_pool)) {
  ON_CALL(*this, upstream()).WillByDefault(Return(conn_pool_));
  ON_CALL(*this, shouldMirror(_)).WillByDefault(Return(true));
}

MockFaultManager::MockFaultManager() = default;
MockFaultManager::MockFaultManager(const MockFaultManager&) {}
MockFaultManager::~MockFaultManager() = default;

namespace ConnPool {

MockPoolCallbacks::MockPoolCallbacks() = default;
MockPoolCallbacks::~MockPoolCallbacks() = default;

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

MockCommandSplitterFactory::MockCommandSplitterFactory() = default;
MockCommandSplitterFactory::~MockCommandSplitterFactory() = default;

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

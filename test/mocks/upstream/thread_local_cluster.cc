#include "thread_local_cluster.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
using ::testing::Return;
using ::testing::ReturnRef;
MockThreadLocalCluster::MockThreadLocalCluster() {
  ON_CALL(*this, prioritySet()).WillByDefault(ReturnRef(cluster_.priority_set_));
  ON_CALL(*this, info()).WillByDefault(Return(cluster_.info_));
  ON_CALL(*this, loadBalancer()).WillByDefault(ReturnRef(lb_));
}

MockThreadLocalCluster::~MockThreadLocalCluster() = default;

} // namespace Upstream
} // namespace Envoy

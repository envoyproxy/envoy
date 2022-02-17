#include "thread_local_cluster.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Return;
using ::testing::ReturnRef;

namespace Envoy {
namespace Upstream {

MockThreadLocalCluster::MockThreadLocalCluster() {
  ON_CALL(*this, prioritySet()).WillByDefault(ReturnRef(cluster_.priority_set_));
  ON_CALL(*this, info()).WillByDefault(Return(cluster_.info_));
  ON_CALL(*this, loadBalancer()).WillByDefault(ReturnRef(lb_));
  ON_CALL(*this, httpConnPool(_, _, _))
      .WillByDefault(Return(Upstream::HttpPoolData([]() {}, &conn_pool_)));
  ON_CALL(*this, tcpConnPool(_, _))
      .WillByDefault(Return(Upstream::TcpPoolData([]() {}, &tcp_conn_pool_)));
  ON_CALL(*this, httpAsyncClient()).WillByDefault(ReturnRef(async_client_));
}

MockThreadLocalCluster::~MockThreadLocalCluster() = default;

} // namespace Upstream
} // namespace Envoy

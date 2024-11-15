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
  ON_CALL(*this, dropOverload()).WillByDefault(Return(cluster_.drop_overload_));
  ON_CALL(*this, dropCategory()).WillByDefault(ReturnRef(cluster_.drop_category_));
  ON_CALL(*this, setDropOverload(_)).WillByDefault(Invoke([this](UnitFloat drop_overload) -> void {
    cluster_.drop_overload_ = drop_overload;
  }));
  ON_CALL(*this, setDropCategory(_))
      .WillByDefault(Invoke([this](absl::string_view drop_category) -> void {
        cluster_.drop_category_ = drop_category;
      }));
}

MockThreadLocalCluster::~MockThreadLocalCluster() = default;

} // namespace Upstream
} // namespace Envoy

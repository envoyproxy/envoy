#include "test/mocks/router/router_filter_interface.h"

using testing::AnyNumber;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Router {

MockRouterFilterInterface::MockRouterFilterInterface()
    : config_("prefix.", context_, ShadowWriterPtr(new MockShadowWriter()), router_proto) {
  auto cluster_info = new NiceMock<Upstream::MockClusterInfo>();
  cluster_info->timeout_budget_stats_ = nullptr;
  ON_CALL(*cluster_info, timeoutBudgetStats()).WillByDefault(Return(absl::nullopt));
  cluster_info_.reset(cluster_info);
  ON_CALL(*this, callbacks()).WillByDefault(Return(&callbacks_));
  ON_CALL(*this, config()).WillByDefault(ReturnRef(config_));
  ON_CALL(*this, cluster()).WillByDefault(Return(cluster_info_));
  ON_CALL(*this, upstreamRequests()).WillByDefault(ReturnRef(requests_));
  EXPECT_CALL(callbacks_.dispatcher_, setTrackedObject(_)).Times(AnyNumber());
  ON_CALL(*this, routeEntry()).WillByDefault(Return(&route_entry_));
  ON_CALL(callbacks_, connection()).WillByDefault(Return(&client_connection_));
  route_entry_.connect_config_.emplace(RouteEntry::ConnectConfig());
}

MockRouterFilterInterface::~MockRouterFilterInterface() = default;

} // namespace Router
} // namespace Envoy

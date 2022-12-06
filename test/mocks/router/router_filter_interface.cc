#include "test/mocks/router/router_filter_interface.h"

using testing::AnyNumber;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Router {

MockRouterFilterInterface::MockRouterFilterInterface()
    : pool_(*symbol_table_), config_(pool_.add("prefix"), context_,
                                     ShadowWriterPtr(new MockShadowWriter()), router_proto) {
  auto cluster_info = new NiceMock<Upstream::MockClusterInfo>();
  cluster_info->timeout_budget_stats_ = nullptr;
  ON_CALL(*cluster_info, timeoutBudgetStats()).WillByDefault(Return(absl::nullopt));
  cluster_info_.reset(cluster_info);
  ON_CALL(*this, callbacks()).WillByDefault(Return(&callbacks_));
  ON_CALL(*this, config()).WillByDefault(ReturnRef(config_));
  ON_CALL(*this, cluster()).WillByDefault(Return(cluster_info_));
  ON_CALL(*this, upstreamRequests()).WillByDefault(ReturnRef(requests_));
  EXPECT_CALL(callbacks_.dispatcher_, pushTrackedObject(_)).Times(AnyNumber());
  EXPECT_CALL(callbacks_.dispatcher_, popTrackedObject(_)).Times(AnyNumber());
  ON_CALL(*this, route()).WillByDefault(Return(&route_));
  ON_CALL(callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{client_connection_}));
  ON_CALL(*this, timeSource()).WillByDefault(ReturnRef(time_system_));
  route_.route_entry_.connect_config_.emplace(RouteEntry::ConnectConfig());
}

MockRouterFilterInterface::~MockRouterFilterInterface() = default;

} // namespace Router
} // namespace Envoy

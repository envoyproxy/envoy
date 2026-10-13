#include "contrib/rocketmq_proxy/filters/network/test/mocks.h"

#include "contrib/rocketmq_proxy/filters/network/source/router/router_impl.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

MockActiveMessage::MockActiveMessage(ConnectionManager& conn_manager, RemotingCommandPtr&& request)
    : ActiveMessage(conn_manager, std::move(request)) {
  route_ = std::make_shared<NiceMock<Router::MockRoute>>();

  ON_CALL(*this, onError(_)).WillByDefault(Invoke([&](absl::string_view error_message) {
    ActiveMessage::onError(error_message);
  }));
  ON_CALL(*this, onReset()).WillByDefault(Return());
  ON_CALL(*this, sendResponseToDownstream()).WillByDefault(Invoke([&]() {
    ActiveMessage::sendResponseToDownstream();
  }));
  ON_CALL(*this, metadata()).WillByDefault(Invoke([&]() { return ActiveMessage::metadata(); }));
  ON_CALL(*this, route()).WillByDefault(Return(route_));
}
MockActiveMessage::~MockActiveMessage() = default;

MockConfig::MockConfig()
    : stats_(RocketmqFilterStats::generateStats("test.", *store_.rootScope())) {
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, createRouter())
      .WillByDefault(Return(ByMove(std::make_unique<Router::RouterImpl>(cluster_manager_))));
  ON_CALL(*this, developMode()).WillByDefault(Return(false));
  ON_CALL(*this, proxyAddress()).WillByDefault(Return(std::string{"1.2.3.4:1234"}));
}

namespace Router {

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
}

MockRouteEntry::~MockRouteEntry() = default;

MockRoute::MockRoute() { ON_CALL(*this, routeEntry()).WillByDefault(Return(&route_entry_)); }
MockRoute::~MockRoute() = default;

} // namespace Router
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

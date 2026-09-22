#include "test/mocks/config/xds_manager.h"

#include "source/common/common/cleanup.h"

namespace Envoy {
namespace Config {
using testing::_;
using testing::Return;

MockXdsManager::MockXdsManager() {
  ON_CALL(*this, initialize(_, _)).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, initializeAdsConnections(_)).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, adsMux()).WillByDefault(Return(ads_mux_));
  ON_CALL(*this, subscriptionFactory()).WillByDefault(ReturnRef(subscription_factory_));
  ON_CALL(*this, pause(testing::Matcher<const std::string&>(_)))
      .WillByDefault(Return(testing::ByMove(std::make_unique<Cleanup>([] {}))));
  ON_CALL(*this, pause(testing::Matcher<const std::vector<std::string>&>(_)))
      .WillByDefault(Return(testing::ByMove(std::make_unique<Cleanup>([] {}))));
}

} // namespace Config
} // namespace Envoy

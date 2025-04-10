#include "test/mocks/config/xds_manager.h"

namespace Envoy {
namespace Config {
using testing::_;
using testing::Return;

MockXdsManager::MockXdsManager() {
  ON_CALL(*this, initialize(_, _)).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, subscriptionFactory()).WillByDefault(ReturnRef(subscription_factory_));
}

} // namespace Config
} // namespace Envoy

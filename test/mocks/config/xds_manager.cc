#include "test/mocks/config/xds_manager.h"

namespace Envoy {
namespace Config {
using testing::_;
using testing::Return;

MockXdsManager::MockXdsManager() {
  ON_CALL(*this, initialize(_)).WillByDefault(Return(absl::OkStatus()));
}

} // namespace Config
} // namespace Envoy

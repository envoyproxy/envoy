#include "test/extensions/clusters/redis/mocks.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

MockClusterSlotUpdateCallBack::MockClusterSlotUpdateCallBack() {
  ON_CALL(*this, onClusterSlotUpdate(_, _)).WillByDefault(Return(true));
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

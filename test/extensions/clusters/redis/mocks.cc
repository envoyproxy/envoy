#include "test/extensions/clusters/redis/mocks.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

MockRedisCluster::MockRedisCluster(Upstream::HostSharedPtr host) {
  slot_array_.fill(host);
  ON_CALL(*this, slotArray()).WillByDefault(ReturnRef(slot_array_));
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

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

MockClusterSlotUpdateCallBack::MockClusterSlotUpdateCallBack() {
  ON_CALL(*this, onClusterSlotUpdate(_, _))
      .WillByDefault(
          Invoke([&](const std::vector<ClusterSlot>& slots, Upstream::HostMap all_hosts) -> bool {
            EXPECT_FALSE(slots.empty());
            EXPECT_FALSE(all_hosts.empty());
            return true;
          }));
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

#include "envoy/upstream/upstream.h"

#include "source/extensions/clusters/redis/redis_cluster.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class MockRedisCluster : public RedisCluster, public Upstream::MockClusterMockPrioritySet {
public:
  MockRedisCluster(Upstream::HostSharedPtr host);
  ~MockRedisCluster(){};

  MOCK_CONST_METHOD0(slotArray, const SlotArray&());

private:
  SlotArray slot_array_;
};

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

#include "envoy/upstream/upstream.h"

#include "source/extensions/clusters/redis/redis_cluster.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class MockClusterSlotUpdateCallBack : public ClusterSlotUpdateCallBack {
public:
  MockClusterSlotUpdateCallBack();
  ~MockClusterSlotUpdateCallBack() = default;

  MOCK_METHOD2(onClusterSlotUpdate, bool(const std::vector<ClusterSlot>&, Upstream::HostMap));
};

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

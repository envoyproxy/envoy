#include "envoy/config/cluster/redis/redis_cluster.pb.h"
#include "envoy/config/cluster/redis/redis_cluster.pb.validate.h"
#include "envoy/extensions/filters/network/redis_proxy/v3alpha/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3alpha/redis_proxy.pb.validate.h"
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
  ~MockClusterSlotUpdateCallBack() override = default;

  MOCK_METHOD2(onClusterSlotUpdate, bool(ClusterSlotsPtr&&, Upstream::HostMap));
  MOCK_METHOD0(onHostHealthUpdate, void());
};

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

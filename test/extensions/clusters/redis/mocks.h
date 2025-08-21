#pragma once

#include "envoy/extensions/clusters/redis/v3/redis_cluster.pb.h"
#include "envoy/extensions/clusters/redis/v3/redis_cluster.pb.validate.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/upstream/upstream.h"

#include "source/extensions/clusters/redis/redis_cluster.h"

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

  MOCK_METHOD(bool, onClusterSlotUpdate, (ClusterSlotsSharedPtr&&, Upstream::HostMap&));
  MOCK_METHOD(void, onHostHealthUpdate, ());
};

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

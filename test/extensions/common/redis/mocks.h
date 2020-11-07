#pragma once

#include "extensions/common/redis/cluster_refresh_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

class MockClusterRefreshManager : public ClusterRefreshManager {
public:
  MockClusterRefreshManager();
  ~MockClusterRefreshManager() override;

  MOCK_METHOD(bool, onRedirection, (const std::string& cluster_name));
  MOCK_METHOD(bool, onFailure, (const std::string& cluster_name));
  MOCK_METHOD(bool, onHostDegraded, (const std::string& cluster_name));
  MOCK_METHOD(HandlePtr, registerCluster,
              (const std::string& cluster_name,
               std::chrono::milliseconds min_time_between_triggering,
               const uint32_t redirects_threshold, const uint32_t failure_threshold,
               const uint32_t host_degraded_threshold, const RefreshCB& cb));
};

} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "extensions/common/redis/redirection_mgr.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

class MockRedirectionManager : public RedirectionManager {
public:
  MockRedirectionManager();
  ~MockRedirectionManager() override;

  MOCK_METHOD1(onRedirection, bool(const std::string& cluster_name));
  MOCK_METHOD4(registerCluster, HandlePtr(const std::string& cluster_name,
                                          std::chrono::milliseconds min_time_between_triggering,
                                          uint32_t redirects_threshold, const RedirectCB& cb));
};

} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy

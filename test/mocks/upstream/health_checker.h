#pragma once

#include "envoy/upstream/health_checker.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockHealthChecker : public HealthChecker {
public:
  MockHealthChecker();
  ~MockHealthChecker() override;

  MOCK_METHOD(void, addHostCheckCompleteCb, (HostStatusCb callback));
  MOCK_METHOD(void, start, ());

  void runCallbacks(Upstream::HostSharedPtr host, HealthTransition changed_state) {
    for (const auto& callback : callbacks_) {
      callback(host, changed_state);
    }
  }

  std::list<HostStatusCb> callbacks_;
};
} // namespace Upstream
} // namespace Envoy

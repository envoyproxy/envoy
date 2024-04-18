#include "health_checker.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
using ::testing::_;
using ::testing::Invoke;
MockHealthChecker::MockHealthChecker() {
  ON_CALL(*this, addHostCheckCompleteCb(_)).WillByDefault(Invoke([this](HostStatusCb cb) -> void {
    callbacks_.push_back(cb);
  }));
}

MockHealthChecker::~MockHealthChecker() = default;

} // namespace Upstream
} // namespace Envoy

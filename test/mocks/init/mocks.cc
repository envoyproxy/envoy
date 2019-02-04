#include "mocks.h"

#include <functional>

#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Init {

MockTarget::MockTarget() {
  ON_CALL(*this, initialize(_))
      .WillByDefault(Invoke([this](std::function<void()> callback) -> void {
        EXPECT_EQ(nullptr, callback_);
        callback_ = callback;
      }));
}

MockTarget::~MockTarget() {}

MockManager::MockManager() {
  ON_CALL(*this, registerTarget(_)).WillByDefault(Invoke([this](Target& target) -> void {
    targets_.push_back(&target);
  }));
}

MockManager::~MockManager() {}

} // namespace Init
} // namespace Envoy

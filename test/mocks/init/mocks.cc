#include "mocks.h"

#include <functional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::_;

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

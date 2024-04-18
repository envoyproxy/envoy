#include "cluster.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
MockCluster::MockCluster() {
  ON_CALL(*this, info()).WillByDefault(Return(info_));
  ON_CALL(*this, initialize(_))
      .WillByDefault(Invoke([this](std::function<void()> callback) -> void {
        EXPECT_EQ(nullptr, initialize_callback_);
        initialize_callback_ = callback;
      }));
  ON_CALL(*this, dropOverload()).WillByDefault(Return(drop_overload_));
  ON_CALL(*this, setDropOverload(_)).WillByDefault(Invoke([this](UnitFloat drop_overload) -> void {
    drop_overload_ = drop_overload;
  }));
}

MockCluster::~MockCluster() = default;

} // namespace Upstream
} // namespace Envoy

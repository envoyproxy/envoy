#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnArg;

namespace Envoy {
namespace Runtime {

MockSnapshot::MockSnapshot() {
  ON_CALL(*this, getInteger(_, _)).WillByDefault(ReturnArg<1>());
  ON_CALL(*this, getDouble(_, _)).WillByDefault(ReturnArg<1>());
  ON_CALL(*this, getBoolean(_, _)).WillByDefault(ReturnArg<1>());
  ON_CALL(*this, get(_)).WillByDefault(Return(absl::nullopt));
}

MockSnapshot::~MockSnapshot() = default;

MockLoader::MockLoader() {
  ON_CALL(*this, threadsafeSnapshot()).WillByDefault(testing::Invoke([]() {
    return std::make_shared<const NiceMock<MockSnapshot>>();
  }));
  ON_CALL(*this, snapshot()).WillByDefault(ReturnRef(snapshot_));
  ON_CALL(*this, getRootScope()).WillByDefault(ReturnRef(store_));
}

MockLoader::~MockLoader() = default;

MockOverrideLayer::MockOverrideLayer() = default;

MockOverrideLayer::~MockOverrideLayer() = default;

} // namespace Runtime
} // namespace Envoy

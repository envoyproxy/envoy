#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnArg;

namespace Envoy {
namespace Runtime {

MockRandomGenerator::MockRandomGenerator() { ON_CALL(*this, uuid()).WillByDefault(Return(uuid_)); }

MockRandomGenerator::~MockRandomGenerator() = default;

MockSnapshot::MockSnapshot() {
  ON_CALL(*this, getInteger(_, _)).WillByDefault(ReturnArg<1>());
  ON_CALL(*this, getDouble(_, _)).WillByDefault(ReturnArg<1>());
  ON_CALL(*this, getBoolean(_, _)).WillByDefault(ReturnArg<1>());
  ON_CALL(*this, get(_)).WillByDefault(Return(absl::nullopt));
}

MockSnapshot::~MockSnapshot() = default;

MockLoader::MockLoader() { ON_CALL(*this, snapshot()).WillByDefault(ReturnRef(snapshot_)); }

MockLoader::~MockLoader() = default;

MockOverrideLayer::MockOverrideLayer() = default;

MockOverrideLayer::~MockOverrideLayer() = default;

} // namespace Runtime
} // namespace Envoy

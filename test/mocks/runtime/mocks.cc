#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::An;
using testing::Invoke;
using testing::Return;
using testing::ReturnArg;

namespace Envoy {
namespace Runtime {

MockRandomGenerator::MockRandomGenerator() { ON_CALL(*this, uuid()).WillByDefault(Return(uuid_)); }

MockRandomGenerator::~MockRandomGenerator() {}

MockSnapshot::MockSnapshot() {
  ON_CALL(*this, getInteger(_, _)).WillByDefault(ReturnArg<1>());
  ON_CALL(*this, featureEnabled(_, An<uint64_t>()))
      .WillByDefault(Invoke(this, &MockSnapshot::featureEnabledDefault));
}

MockSnapshot::~MockSnapshot() {}

MockLoader::MockLoader() { ON_CALL(*this, snapshot()).WillByDefault(ReturnRef(snapshot_)); }

MockLoader::~MockLoader() {}

MockOverrideLayer::MockOverrideLayer() {}

MockOverrideLayer::~MockOverrideLayer() {}

} // namespace Runtime
} // namespace Envoy

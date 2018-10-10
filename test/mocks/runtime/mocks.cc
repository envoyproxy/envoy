#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnArg;

namespace Envoy {
namespace Runtime {

MockRandomGenerator::MockRandomGenerator() { ON_CALL(*this, uuid()).WillByDefault(Return(uuid_)); }

MockRandomGenerator::~MockRandomGenerator() {}

MockSnapshot::MockSnapshot() { ON_CALL(*this, getInteger(_, _)).WillByDefault(ReturnArg<1>()); }

MockSnapshot::~MockSnapshot() {}

MockLoader::MockLoader() { ON_CALL(*this, snapshot()).WillByDefault(ReturnRef(snapshot_)); }

MockLoader::~MockLoader() {}

MockOverrideLayer::MockOverrideLayer() {}

MockOverrideLayer::~MockOverrideLayer() {}

} // namespace Runtime
} // namespace Envoy

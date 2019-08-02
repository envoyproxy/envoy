#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnArg;
using testing::ReturnRef;

namespace Envoy {
namespace Runtime {

MockRandomGenerator::MockRandomGenerator() { ON_CALL(*this, uuid()).WillByDefault(Return(uuid_)); }

MockRandomGenerator::~MockRandomGenerator() = default;

MockSnapshot::MockSnapshot() { ON_CALL(*this, getInteger(_, _)).WillByDefault(ReturnArg<1>()); }

MockSnapshot::~MockSnapshot() = default;

MockLoader::MockLoader()
    : threadsafe_snapshot_(std::make_shared<NiceMock<MockSnapshot>>()),
      snapshot_(*threadsafe_snapshot_) {
  ON_CALL(*this, snapshot()).WillByDefault(ReturnRef(snapshot_));
  ON_CALL(*this, threadsafeSnapshot()).WillByDefault(Return(threadsafe_snapshot_));
}

MockLoader::~MockLoader() = default;

MockOverrideLayer::MockOverrideLayer() = default;

MockOverrideLayer::~MockOverrideLayer() = default;

ScopedMockLoaderSingleton::ScopedMockLoaderSingleton() { LoaderSingleton::initialize(&loader_); }

ScopedMockLoaderSingleton::~ScopedMockLoaderSingleton() { LoaderSingleton::clear(); }

Runtime::MockLoader& ScopedMockLoaderSingleton::loader() { return loader_; }

Runtime::MockSnapshot& ScopedMockLoaderSingleton::snapshot() { return loader_.snapshot_; }

} // namespace Runtime
} // namespace Envoy

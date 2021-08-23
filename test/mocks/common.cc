#include "test/mocks/common.h"

using testing::Return;

namespace Envoy {
namespace ConnectionPool {
MockCancellable::MockCancellable() = default;
MockCancellable::~MockCancellable() = default;
} // namespace ConnectionPool

namespace Random {

MockRandomGenerator::MockRandomGenerator() { ON_CALL(*this, uuid()).WillByDefault(Return(uuid_)); }

MockRandomGenerator::MockRandomGenerator(uint64_t value) : value_(value) {
  ON_CALL(*this, random()).WillByDefault(Return(value_));
  ON_CALL(*this, uuid()).WillByDefault(Return(uuid_));
}

MockRandomGenerator::~MockRandomGenerator() = default;

} // namespace Random

ReadyWatcher::ReadyWatcher() = default;
ReadyWatcher::~ReadyWatcher() = default;

MockTimeSystem::MockTimeSystem() = default;
MockTimeSystem::~MockTimeSystem() = default;

} // namespace Envoy

#include "test/mocks/common.h"

namespace Envoy {
namespace ConnectionPool {
MockCancellable::MockCancellable() = default;
MockCancellable::~MockCancellable() = default;
} // namespace ConnectionPool

ReadyWatcher::ReadyWatcher() = default;
ReadyWatcher::~ReadyWatcher() = default;

MockTimeSystem::MockTimeSystem() = default;
MockTimeSystem::~MockTimeSystem() = default;

} // namespace Envoy

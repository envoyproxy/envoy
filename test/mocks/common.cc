#include "test/mocks/common.h"

namespace Envoy {
ReadyWatcher::ReadyWatcher() = default;
ReadyWatcher::~ReadyWatcher() = default;

MockTimeSystem::MockTimeSystem() = default;
MockTimeSystem::~MockTimeSystem() = default;

} // namespace Envoy

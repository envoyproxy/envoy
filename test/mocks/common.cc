#include "test/mocks/common.h"

namespace Envoy {
ReadyWatcher::ReadyWatcher() {}
ReadyWatcher::~ReadyWatcher() {}

MockTimeSystem::MockTimeSystem() {}
MockTimeSystem::~MockTimeSystem() {}

MockTokenBucket::MockTokenBucket() {}
MockTokenBucket::~MockTokenBucket() {}

} // namespace Envoy

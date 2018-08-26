#include "test/mocks/common.h"

namespace Envoy {
ReadyWatcher::ReadyWatcher() {}
ReadyWatcher::~ReadyWatcher() {}

MockTimeSource::MockTimeSource() {}
MockTimeSource::~MockTimeSource() {}

MockTimeSystem::MockTimeSystem() {}
MockTimeSystem::~MockTimeSystem() {}

MockTokenBucket::MockTokenBucket() {}
MockTokenBucket::~MockTokenBucket() {}

} // namespace Envoy
